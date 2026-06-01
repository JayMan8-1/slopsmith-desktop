// AudioChannel — Windows backend: named file-mapping shm + a pair of named
// auto-reset events. The ring algorithm lives in AudioChannel_shared.cpp; this
// file implements only create/open/close and the doorbell (signalEvent /
// waitEvent map to SetEvent / WaitForSingleObject).

#include "AudioChannelImpl.h"
#include "../VSTTrace.h"

#if ! JUCE_WINDOWS
 #error "AudioChannel_win.cpp is Windows-only; POSIX builds use AudioChannel_posix.cpp."
#endif

#include <cstring>

namespace slopsmith::sandbox {

static juce::String makeUniqueName(const char* suffix)
{
    return "Local\\slopsmith-vst-" + juce::Uuid().toDashedString() + "-" + suffix;
}

bool AudioChannel::createHostSide(const AudioDimensions& dims, Names& namesOut,
                                  juce::String& errorOut)
{
    namesOut.shm          = makeUniqueName(kShmNameSuffix);
    namesOut.evtToHost    = makeUniqueName(kEvtToHostSuffix);
    namesOut.evtToSandbox = makeUniqueName(kEvtToSandboxSuffix);

    auto totalBytes = dims.totalShmBytes();
    impl->mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        (DWORD)(totalBytes >> 32), (DWORD)(totalBytes & 0xFFFFFFFFu),
        namesOut.shm.toWideCharPointer());
    if (impl->mapping == nullptr)
    {
        errorOut = "CreateFileMapping failed: " + juce::String((int)GetLastError());
        close();
        return false;
    }
    impl->view = MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, totalBytes);
    if (!impl->view)
    {
        errorOut = "MapViewOfFile failed";
        close();
        return false;
    }
    impl->header = reinterpret_cast<AudioShmHeader*>(impl->view);

    // Initialise the header on the host side.
    impl->header->magic = kAudioShmMagic;
    impl->header->protocolVersion = kProtocolVersion;
    impl->header->maxBlocks = dims.maxBlocks;
    impl->header->maxBlockSamples = dims.maxBlockSamples;
    impl->header->maxChannels = dims.maxChannels;
    impl->header->sampleRate = dims.sampleRate;
    impl->header->inWriteIdx  = 0;
    impl->header->inReadIdx   = 0;
    impl->header->outWriteIdx = 0;
    impl->header->outReadIdx  = 0;
    impl->header->xruns = 0;
    impl->header->dropouts = 0;
    impl->header->midiOverflows = 0;
    impl->header->ringBytesPerSlot = dims.bytesPerSlot();
    impl->header->inputRingOffset  = sizeof(AudioShmHeader);
    impl->header->outputRingOffset = impl->header->inputRingOffset
                                   + uint64_t(dims.maxBlocks) * dims.bytesPerSlot();
    impl->header->midiQueueOffset  = impl->header->outputRingOffset
                                   + uint64_t(dims.maxBlocks) * dims.bytesPerSlot();

    // Release fence so all the header writes above are visible before the
    // sandbox observes the mapping. CreateProcessW (the publish point on
    // the host side) is a strong synchronisation primitive on Windows, so
    // in practice the writes are already flushed before the child starts —
    // but the fence makes the spawn-order invariant documented in
    // openSandboxSide explicit at the producer rather than relying on the
    // implicit semantics of the spawn call.
    std::atomic_thread_fence(std::memory_order_release);

    auto* base = reinterpret_cast<char*>(impl->view);
    impl->inputRing  = reinterpret_cast<float*>(base + impl->header->inputRingOffset);
    impl->outputRing = reinterpret_cast<float*>(base + impl->header->outputRingOffset);
    impl->midiQueues = reinterpret_cast<MidiQueue*>(base + impl->header->midiQueueOffset);
    // Zero-initialise the per-slot MidiQueues so a producer's first publish
    // doesn't have to clear count/overflow bookkeeping.
    std::memset(impl->midiQueues, 0,
                sizeof(MidiQueue) * (size_t)dims.maxBlocks);

    impl->evtToHost = CreateEventW(
        nullptr, /*manualReset*/FALSE, /*initial*/FALSE,
        namesOut.evtToHost.toWideCharPointer());
    impl->evtToSandbox = CreateEventW(
        nullptr, FALSE, FALSE,
        namesOut.evtToSandbox.toWideCharPointer());
    if (!impl->evtToHost || !impl->evtToSandbox)
    {
        errorOut = "CreateEvent failed";
        close();
        return false;
    }
    cachedDims = dims;
    return true;
}

bool AudioChannel::openSandboxSide(const Names& names, juce::String& errorOut)
{
    impl->mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
                                     names.shm.toWideCharPointer());
    if (!impl->mapping)
    {
        errorOut = "OpenFileMapping failed: " + juce::String((int)GetLastError());
        close();
        return false;
    }
    impl->view = MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!impl->view)
    {
        errorOut = "MapViewOfFile (sandbox) failed";
        close();
        return false;
    }
    // Before the magic check (which reads header->magic), verify the mapping
    // is at least sizeof(AudioShmHeader). `MapViewOfFile(...,0)` maps the
    // whole object, but if a corrupted/malicious named-mapping pointed at a
    // smaller object the magic check itself would be an OOB read. The
    // expectedTotal bounds check below uses header-derived fields and so
    // cannot detect an undersized real mapping — this is the only place we
    // can close that gap.
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(impl->view, &mbi, sizeof(mbi)) == 0
            || mbi.RegionSize < sizeof(AudioShmHeader))
        {
            errorOut = "audio shm mapping too small for header ("
                     + juce::String((int64_t)mbi.RegionSize) + " < "
                     + juce::String((int64_t)sizeof(AudioShmHeader)) + ")";
            close();
            return false;
        }
    }
    impl->header = reinterpret_cast<AudioShmHeader*>(impl->view);
    if (impl->header->magic != kAudioShmMagic)
    {
        errorOut = "audio shm magic mismatch";
        close();
        return false;
    }
    if (impl->header->protocolVersion != kProtocolVersion)
    {
        errorOut = "audio shm protocol mismatch: expected "
                 + juce::String((int)kProtocolVersion) + ", got "
                 + juce::String((int)impl->header->protocolVersion);
        close();
        return false;
    }
    // Validate dims against compile-time caps BEFORE computing bytesPerSlot()
    // and expectedTotal. Without this, pathological header values (corrupted
    // or malicious mapping that passed magic+protocolVersion) can overflow
    // uint64_t in `maxBlockSamples * maxChannels * 4` or
    // `2 * maxBlocks * ringBytesPerSlot`, defeating the inEnd/outEnd bounds
    // check below and pointing inputRing/outputRing past the actual mapping.
    if (impl->header->maxBlocks == 0 || impl->header->maxBlocks > kAudioMaxBlocks
        || impl->header->maxBlockSamples == 0
        || impl->header->maxBlockSamples > kAudioMaxBlockSamples
        || impl->header->maxChannels == 0
        || impl->header->maxChannels > kAudioMaxChannels)
    {
        errorOut = "audio shm dims exceed protocol caps: blocks="
                 + juce::String((int64_t)impl->header->maxBlocks)
                 + " blockSamples=" + juce::String((int64_t)impl->header->maxBlockSamples)
                 + " channels=" + juce::String((int64_t)impl->header->maxChannels);
        close();
        return false;
    }
    cachedDims.maxBlocks = impl->header->maxBlocks;
    cachedDims.maxBlockSamples = impl->header->maxBlockSamples;
    cachedDims.maxChannels = impl->header->maxChannels;
    cachedDims.sampleRate = impl->header->sampleRate;

    // Cross-check ringBytesPerSlot against the dims the host published. A
    // mismatch here would silently produce misaligned ring access — the
    // protocol version check upstream already guarantees host/sandbox
    // agree on the layout, this just makes the contract local.
    const uint64_t expectedSlotBytes = cachedDims.bytesPerSlot();
    if (impl->header->ringBytesPerSlot != expectedSlotBytes)
    {
        errorOut = "audio shm ringBytesPerSlot mismatch: expected "
                 + juce::String((int64_t)expectedSlotBytes) + ", got "
                 + juce::String((int64_t)impl->header->ringBytesPerSlot);
        close();
        return false;
    }

    // Spawn-order invariant: the host fully initialises the shared header
    // BEFORE calling CreateProcessW, so by the time the sandbox observes the
    // mapping the header is fully populated.

    const uint64_t expectedTotal = sizeof(AudioShmHeader)
        + 2 * uint64_t(impl->header->maxBlocks) * impl->header->ringBytesPerSlot
        + uint64_t(impl->header->maxBlocks) * sizeof(MidiQueue);
    const uint64_t inEnd  = impl->header->inputRingOffset
        + uint64_t(impl->header->maxBlocks) * impl->header->ringBytesPerSlot;
    const uint64_t outEnd = impl->header->outputRingOffset
        + uint64_t(impl->header->maxBlocks) * impl->header->ringBytesPerSlot;
    const uint64_t midiEnd = impl->header->midiQueueOffset
        + uint64_t(impl->header->maxBlocks) * sizeof(MidiQueue);
    // Bounds + ordering check: each region must fit within the mapping and
    // not overlap. The canonical layout is: [header][input][output][midi].
    if (inEnd > expectedTotal
        || outEnd > expectedTotal
        || midiEnd > expectedTotal
        || impl->header->inputRingOffset  < sizeof(AudioShmHeader)
        || impl->header->outputRingOffset < inEnd
        || impl->header->midiQueueOffset  < outEnd)
    {
        errorOut = "audio shm ring/MIDI offsets out of bounds or overlapping";
        close();
        return false;
    }
    // Verify the actual mapped region is at least expectedTotal. The earlier
    // VirtualQuery covered just sizeof(AudioShmHeader).
    {
        MEMORY_BASIC_INFORMATION mbi2{};
        if (VirtualQuery(impl->view, &mbi2, sizeof(mbi2)) == 0
            || mbi2.RegionSize < expectedTotal)
        {
            errorOut = "audio shm mapping too small for ring layout: region="
                     + juce::String((int64_t)mbi2.RegionSize) + " expected>="
                     + juce::String((int64_t)expectedTotal);
            close();
            return false;
        }
    }

    auto* base = reinterpret_cast<char*>(impl->view);
    impl->inputRing  = reinterpret_cast<float*>(base + impl->header->inputRingOffset);
    impl->outputRing = reinterpret_cast<float*>(base + impl->header->outputRingOffset);
    impl->midiQueues = reinterpret_cast<MidiQueue*>(base + impl->header->midiQueueOffset);

    impl->evtToHost    = OpenEventW(EVENT_ALL_ACCESS, FALSE,
                                    names.evtToHost.toWideCharPointer());
    impl->evtToSandbox = OpenEventW(EVENT_ALL_ACCESS, FALSE,
                                    names.evtToSandbox.toWideCharPointer());
    if (!impl->evtToHost || !impl->evtToSandbox)
    {
        errorOut = "OpenEvent failed: " + juce::String((int)GetLastError());
        close();
        return false;
    }
    return true;
}

void AudioChannel::close()
{
    if (impl->evtToHost)    { CloseHandle(impl->evtToHost);    impl->evtToHost = nullptr; }
    if (impl->evtToSandbox) { CloseHandle(impl->evtToSandbox); impl->evtToSandbox = nullptr; }
    if (impl->view)         { UnmapViewOfFile(impl->view);     impl->view = nullptr; }
    if (impl->mapping)      { CloseHandle(impl->mapping);      impl->mapping = nullptr; }
    impl->header = nullptr;
    impl->inputRing = nullptr;
    impl->outputRing = nullptr;
    impl->midiQueues = nullptr;
}

void AudioChannel::Impl::signalEvent(bool isOutputRing)
{
    // The `if (handle)` guard is non-atomic, so a concurrent close() racing
    // this call could in principle observe a freed handle. Today's call paths
    // (the audio producer push + AudioPauseGuard ctor's signalSandboxWake +
    // dispatchRequest's kShutdown / disconnect callback) are serialised
    // against close() by audioThread.join() + control.stop() on the WinMain
    // thread before close() runs. A future caller outside those teardown
    // invariants needs an atomic<HANDLE> swapped to nullptr by close().
    HANDLE evt = isOutputRing ? evtToHost : evtToSandbox;
    if (evt) SetEvent(evt);
}

bool AudioChannel::Impl::waitEvent(bool isOutputRing, int timeoutMs)
{
    HANDLE evt = isOutputRing ? evtToHost : evtToSandbox;
    if (!evt) return false;
    return WaitForSingleObject(evt, (DWORD)timeoutMs) == WAIT_OBJECT_0;
}

void AudioChannel::Impl::wakeSelf()
{
    // The sandbox worker waits on evtToSandbox (the input doorbell). A Win32
    // auto-reset event self-signals fine — SetEvent from this process's
    // control thread wakes this process's worker, and the host (which waits on
    // the separate evtToHost) is unaffected. (POSIX needs a dedicated self-pipe
    // instead; see AudioChannel_posix.cpp.)
    if (evtToSandbox) SetEvent(evtToSandbox);
}

} // namespace slopsmith::sandbox
