// AudioChannel — POSIX backend (macOS + Linux).
//
// Shared memory: an *anonymous* object — shm_open(O_CREAT|O_EXCL), ftruncate,
// then shm_unlink immediately. The fd keeps the object alive (like a deleted-
// but-open file), so there is no lingering /dev/shm name to leak if the
// sandbox crashes, and the macOS 31-char shm-name limit is irrelevant past
// creation. The fd is handed to the sandbox by inheritance (SubprocessHandle
// dup2()s it across posix_spawn) rather than re-opened by name.
//
// Doorbell: a single bidirectional socketpair per side. A socketpair delivers
// each end only what the *other* end wrote, so one fd multiplexes both
// directions — signalEvent writes one byte to wake the peer's waitEvent;
// waitEvent poll()s then drains. This is the only cross-process auto-reset-
// style primitive that is (a) implemented on macOS (unnamed POSIX semaphores
// are not — sem_init returns ENOSYS), (b) crash-safe (a process-shared pthread
// mutex/condvar has no robust-mutex support on macOS, so a producer crash
// holding the lock would deadlock the consumer; a dead socketpair peer instead
// surfaces as POLLHUP), and (c) tolerant of coalesced signals (the ring
// consumers re-read the atomic index on wake, exactly as on the Win32 auto-
// reset path).

#include "AudioChannelImpl.h"
#include "../VSTTrace.h"

#if JUCE_WINDOWS
 #error "AudioChannel_posix.cpp is POSIX-only; Windows builds use AudioChannel_win.cpp."
#endif

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace slopsmith::sandbox {

namespace {

// shm_open names must be globally unique and — on macOS — at most 31 chars
// (PSHMNAMLEN), including the leading '/'. We unlink immediately after
// creation, so the name only has to survive the open() call; pid + a
// per-process counter is unique enough. "/slsv-<pid>-<n>" stays well under 31.
juce::String makeShortShmName()
{
    static std::atomic<unsigned> counter{0};
    const unsigned n = counter.fetch_add(1, std::memory_order_relaxed);
    return "/slsv-" + juce::String((int)getpid()) + "-" + juce::String((int)n);
}

void setNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void setCloExec(int fd)
{
    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

// Suppress SIGPIPE on this socket. macOS has the per-socket SO_NOSIGPIPE
// option; Linux has no such option (it uses MSG_NOSIGNAL on send() — see
// writeDoorbell). A dead doorbell peer must surface as a write error /
// POLLHUP, never as a process-killing signal.
void setNoSigPipe([[maybe_unused]] int fd)
{
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

void prepDoorbellFd(int fd)
{
    setNonBlocking(fd);
    setCloExec(fd);
    setNoSigPipe(fd);
}

// Create this side's same-process self-wake channel. A socketpair (not a bare
// pipe) so wakeSelf's write can be SIGPIPE-suppressed (SO_NOSIGPIPE /
// MSG_NOSIGNAL) like the cross-process doorbell — a write after the read end
// is closed must never raise SIGPIPE. Read end non-blocking so the drain loop
// terminates; both ends CLOEXEC (each process makes its own, never inherited).
bool makeSelfWakePipe(int fds[2], juce::String& errorOut)
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    {
        errorOut = "self-wake socketpair() failed: " + juce::String(strerror(errno));
        return false;
    }
    setNonBlocking(fds[0]);
    setCloExec(fds[0]);
    setNoSigPipe(fds[0]);
    setCloExec(fds[1]);
    setNoSigPipe(fds[1]);
    return true;
}

} // namespace

bool AudioChannel::createHostSide(const AudioDimensions& dims, Names& namesOut,
                                  juce::String& errorOut)
{
    // Windows event names are unused on POSIX; clear them so a stray reader
    // can't mistake them for live handles.
    namesOut.shm.clear();
    namesOut.evtToHost.clear();
    namesOut.evtToSandbox.clear();

    const uint64_t totalBytes = dims.totalShmBytes();

    // Create the anonymous shm object: open exclusively, then unlink so only
    // the fd keeps it alive. Retry on the (vanishingly unlikely) name clash.
    int fd = -1;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const juce::String name = makeShortShmName();
        fd = shm_open(name.toRawUTF8(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
        {
            shm_unlink(name.toRawUTF8()); // anonymous from here on
            break;
        }
        if (errno != EEXIST)
        {
            errorOut = "shm_open failed: " + juce::String(strerror(errno));
            close();
            return false;
        }
    }
    if (fd < 0)
    {
        errorOut = "shm_open failed: name collisions exhausted";
        close();
        return false;
    }
    impl->shmFd = fd;
    setCloExec(impl->shmFd);

    if (ftruncate(impl->shmFd, (off_t)totalBytes) != 0)
    {
        errorOut = "ftruncate failed: " + juce::String(strerror(errno));
        close();
        return false;
    }
    void* m = mmap(nullptr, (size_t)totalBytes, PROT_READ | PROT_WRITE,
                   MAP_SHARED, impl->shmFd, 0);
    if (m == MAP_FAILED)
    {
        errorOut = "mmap failed: " + juce::String(strerror(errno));
        impl->view = nullptr; // MAP_FAILED is not a valid pointer to munmap
        close();
        return false;
    }
    impl->view = m;
    impl->mappedBytes = (size_t)totalBytes;
    impl->header = reinterpret_cast<AudioShmHeader*>(impl->view);

    // Initialise the header (identical layout to the Windows backend).
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

    // Release fence so the header writes are visible before the sandbox
    // observes the mapping. The spawn (posix_spawn) is the real publish point
    // and is a full barrier, but the fence documents the producer side of the
    // spawn-order invariant explicitly rather than relying on it.
    std::atomic_thread_fence(std::memory_order_release);

    auto* base = reinterpret_cast<char*>(impl->view);
    impl->inputRing  = reinterpret_cast<float*>(base + impl->header->inputRingOffset);
    impl->outputRing = reinterpret_cast<float*>(base + impl->header->outputRingOffset);
    impl->midiQueues = reinterpret_cast<MidiQueue*>(base + impl->header->midiQueueOffset);
    std::memset(impl->midiQueues, 0,
                sizeof(MidiQueue) * (size_t)dims.maxBlocks);

    // Doorbell socketpair: sp[0] stays here (host end), sp[1] goes to the
    // sandbox (handed off via Names, then dup2()'d into the child or consumed
    // directly by an in-process openSandboxSide).
    int sp[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0)
    {
        errorOut = "socketpair failed: " + juce::String(strerror(errno));
        close();
        return false;
    }
    impl->evtFd = sp[0];
    prepDoorbellFd(impl->evtFd);
    prepDoorbellFd(sp[1]);

    if (!makeSelfWakePipe(impl->selfWake, errorOut))
    {
        ::close(sp[1]);
        close();
        return false;
    }

    // Hand off a dup of the shm fd + the sandbox's doorbell end. dup() so the
    // host keeps independent ownership of impl->shmFd: an in-process
    // openSandboxSide closes these in its own close(), and a real spawn dup2()s
    // them into the child and closes its copies, neither of which must disturb
    // the host's fds.
    namesOut.shmFd = dup(impl->shmFd);
    if (namesOut.shmFd < 0)
    {
        errorOut = "dup(shmFd) failed: " + juce::String(strerror(errno));
        ::close(sp[1]);
        close();
        return false;
    }
    setCloExec(namesOut.shmFd);
    namesOut.sandboxAudioFd = sp[1];

    cachedDims = dims;
    return true;
}

bool AudioChannel::openSandboxSide(const Names& names, juce::String& errorOut)
{
    // Take ownership of the handed-off fds. From here, close() releases them.
    impl->shmFd = names.shmFd;
    impl->evtFd = names.sandboxAudioFd;
    if (impl->shmFd < 0 || impl->evtFd < 0)
    {
        errorOut = "openSandboxSide: invalid handoff fds (shmFd="
                 + juce::String(impl->shmFd) + " audioFd="
                 + juce::String(impl->evtFd) + ")";
        close();
        return false;
    }
    setCloExec(impl->shmFd);
    prepDoorbellFd(impl->evtFd);

    if (!makeSelfWakePipe(impl->selfWake, errorOut))
    {
        close();
        return false;
    }

    // fstat reports the true object size (set by the host's ftruncate) — the
    // POSIX analog of the Windows VirtualQuery RegionSize checks, and more
    // precise (object size, not mapped-region size). Reject anything smaller
    // than the header before mapping + dereferencing it.
    struct stat st{};
    if (fstat(impl->shmFd, &st) != 0)
    {
        errorOut = "fstat(shmFd) failed: " + juce::String(strerror(errno));
        close();
        return false;
    }
    const uint64_t objectBytes = (uint64_t)st.st_size;
    if (objectBytes < sizeof(AudioShmHeader))
    {
        errorOut = "audio shm object too small for header ("
                 + juce::String((int64_t)objectBytes) + " < "
                 + juce::String((int64_t)sizeof(AudioShmHeader)) + ")";
        close();
        return false;
    }

    void* m = mmap(nullptr, (size_t)objectBytes, PROT_READ | PROT_WRITE,
                   MAP_SHARED, impl->shmFd, 0);
    if (m == MAP_FAILED)
    {
        errorOut = "mmap (sandbox) failed: " + juce::String(strerror(errno));
        impl->view = nullptr;
        close();
        return false;
    }
    impl->view = m;
    impl->mappedBytes = (size_t)objectBytes;
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
    // / expectedTotal, to keep pathological header values from overflowing the
    // uint64_t arithmetic in the bounds check below.
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

    const uint64_t expectedSlotBytes = cachedDims.bytesPerSlot();
    if (impl->header->ringBytesPerSlot != expectedSlotBytes)
    {
        errorOut = "audio shm ringBytesPerSlot mismatch: expected "
                 + juce::String((int64_t)expectedSlotBytes) + ", got "
                 + juce::String((int64_t)impl->header->ringBytesPerSlot);
        close();
        return false;
    }

    const uint64_t expectedTotal = sizeof(AudioShmHeader)
        + 2 * uint64_t(impl->header->maxBlocks) * impl->header->ringBytesPerSlot
        + uint64_t(impl->header->maxBlocks) * sizeof(MidiQueue);
    const uint64_t inEnd  = impl->header->inputRingOffset
        + uint64_t(impl->header->maxBlocks) * impl->header->ringBytesPerSlot;
    const uint64_t outEnd = impl->header->outputRingOffset
        + uint64_t(impl->header->maxBlocks) * impl->header->ringBytesPerSlot;
    const uint64_t midiEnd = impl->header->midiQueueOffset
        + uint64_t(impl->header->maxBlocks) * sizeof(MidiQueue);
    // Bounds + ordering check: each region must fit within the object and not
    // overlap. Canonical layout: [header][input][output][midi].
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
    // And the real object must be at least expectedTotal — a stale/foreign fd
    // could pass magic+version+caps but be backed by a smaller object.
    if (objectBytes < expectedTotal)
    {
        errorOut = "audio shm object too small for ring layout: object="
                 + juce::String((int64_t)objectBytes) + " expected>="
                 + juce::String((int64_t)expectedTotal);
        close();
        return false;
    }

    auto* base = reinterpret_cast<char*>(impl->view);
    impl->inputRing  = reinterpret_cast<float*>(base + impl->header->inputRingOffset);
    impl->outputRing = reinterpret_cast<float*>(base + impl->header->outputRingOffset);
    impl->midiQueues = reinterpret_cast<MidiQueue*>(base + impl->header->midiQueueOffset);
    return true;
}

void AudioChannel::close()
{
    if (impl->view)   { munmap(impl->view, impl->mappedBytes); impl->view = nullptr; }
    impl->mappedBytes = 0;
    if (impl->shmFd >= 0) { ::close(impl->shmFd); impl->shmFd = -1; }
    if (impl->evtFd >= 0) { ::close(impl->evtFd); impl->evtFd = -1; }
    if (impl->selfWake[0] >= 0) { ::close(impl->selfWake[0]); impl->selfWake[0] = -1; }
    if (impl->selfWake[1] >= 0) { ::close(impl->selfWake[1]); impl->selfWake[1] = -1; }
    impl->header = nullptr;
    impl->inputRing = nullptr;
    impl->outputRing = nullptr;
    impl->midiQueues = nullptr;
}

void AudioChannel::Impl::signalEvent(bool /*isOutputRing*/)
{
    // One socketpair carries both directions: a write here wakes whichever
    // side is poll()ing the *other* end. `isOutputRing` is therefore
    // irrelevant on POSIX. Non-blocking + SIGPIPE-suppressed: a full buffer
    // (EAGAIN) means the peer already has pending wakes it hasn't drained, so
    // dropping this one is harmless (the consumer re-reads the index); a dead
    // peer (EPIPE) is handled by the pop-timeout / disconnect paths.
    if (evtFd < 0) return;
    const unsigned char byte = 1;
#ifdef MSG_NOSIGNAL
    (void)::send(evtFd, &byte, 1, MSG_NOSIGNAL);
#else
    (void)::send(evtFd, &byte, 1, 0); // SO_NOSIGPIPE was set on the fd
#endif
}

bool AudioChannel::Impl::waitEvent(bool /*isOutputRing*/, int timeoutMs)
{
    if (evtFd < 0) return false;
    // Poll the cross-process doorbell (peer signals) AND our own self-wake pipe
    // (same-process signalSandboxWake). Either readable is a wake.
    struct pollfd pfds[2]{};
    pfds[0].fd = evtFd;          pfds[0].events = POLLIN;
    pfds[1].fd = selfWake[0];    pfds[1].events = POLLIN;
    const nfds_t nfds = (selfWake[0] >= 0) ? 2 : 1;

    for (;;)
    {
        const int rc = ::poll(pfds, nfds, timeoutMs);
        if (rc < 0)
        {
            if (errno == EINTR) continue; // restart the wait on a signal
            return false;
        }
        if (rc == 0) return false; // timeout — caller decides if it's a dropout
        break;
    }

    // Drain both fds: coalesced wake bytes mustn't cause spurious wakes on
    // later waits — the caller re-reads the ring index for the real state. A
    // peer hangup (POLLHUP) reads as EOF (0) and falls through as a non-data
    // wake; the caller's index recheck then returns no data.
    auto drain = [](int fd)
    {
        if (fd < 0) return;
        unsigned char scratch[64];
        for (;;)
        {
            const ssize_t n = ::read(fd, scratch, sizeof(scratch));
            if (n > 0) continue;
            if (n < 0 && errno == EINTR) continue;
            break; // 0 (EOF/HUP) or EAGAIN/EWOULDBLOCK — drained
        }
    };
    drain(evtFd);
    drain(selfWake[0]);
    return true;
}

void AudioChannel::Impl::wakeSelf()
{
    // Break OUR OWN waitEvent (the worker's popInputBlock) without a block.
    // Writes the self-pipe, NOT evtFd: a socketpair self-write would land on
    // the peer (host), spuriously waking it and never waking us.
    if (selfWake[1] < 0) return;
    const unsigned char byte = 1;
    // SIGPIPE-safe like signalEvent: the self-wake is a socketpair so a write
    // after the read end closed surfaces as EPIPE, not a process-killing signal.
#ifdef MSG_NOSIGNAL
    (void)::send(selfWake[1], &byte, 1, MSG_NOSIGNAL);
#else
    (void)::send(selfWake[1], &byte, 1, 0); // SO_NOSIGPIPE set on the fd instead
#endif
}

} // namespace slopsmith::sandbox
