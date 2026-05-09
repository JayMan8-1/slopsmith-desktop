# Feature Specification: Slopsmith Desktop App

**Feature Branch**: `001-desktop-app` (retrospective)
**Created**: 2026-05-09
**Status**: Implemented (documented after the fact)
**Input**: A native cross-platform desktop wrapper around Slopsmith
that adds low-latency audio I/O, VST/AU/LV2 hosting, NAM, IR loading,
pitch detection, and a plugin manager.

## User Scenarios & Testing

### User Story 1 — Plug in a guitar and play (Priority: P1)

A user installs Slopsmith Desktop, plugs in a USB / interface guitar
input, picks an audio device + sample rate + buffer size in Settings,
and starts playing a song. Audio reaches their speakers with
low latency, and the highway responds to their pitch / note
detection.

**Why this priority**: This is the core value proposition over the
browser version. Without it the app is just an Electron wrapper.

**Independent Test**: Launch the app, open Settings → Audio, pick an
input/output device, hit "Start Audio". Pluck a string. The pitch
indicator updates, output is audible, latency is ≤ buffer-size
expectations.

**Acceptance Scenarios**:

1. **Given** the app launches and the audio addon loads, **When**
   the user opens Audio Settings, **Then** the device list reflects
   the current platform's backend (ASIO on Win, CoreAudio on Mac,
   JACK+ALSA on Linux).
2. **Given** the user selects a device + sample rate + block size,
   **When** they apply, **Then** `AudioEngine::setAudioDevice(...)`
   is called and audio starts.
3. **Given** audio is running, **When** the user plays a note,
   **Then** `PitchDetector` produces a frequency that the renderer
   uses for note detection.
4. **Given** the native addon is missing, **When** the app boots,
   **Then** Audio Settings shows "audio engine unavailable" and the
   rest of the app still works (Constitution VII).

---

### User Story 2 — Embedded Slopsmith UI with full plugin support (Priority: P1)

The user gets the entire Slopsmith experience (library browser, 3D
highway, all plugins) inside the app, with the same plugins they'd
use in the browser version.

**Why this priority**: Without this the app cannot replace browser
Slopsmith; the audio engine alone is not the product.

**Independent Test**: Boot the app, watch the splash screen show
plugin-load progress, land on the library browser, install one
plugin via the Plugin Manager, restart, see the plugin in the menu.

**Acceptance Scenarios**:

1. **Given** the embedded Python subprocess starts, **When** it
   binds an available port at or above 18000, **Then** the renderer
   loads `http://127.0.0.1:<port>/`.
2. **Given** plugin discovery runs at server boot, **When** the
   splash polls `/api/startup-status`, **Then** the splash shows
   `loaded / total` and the current plugin name.
3. **Given** startup exceeds the 5-minute deadline, **When** the
   poll loop ages out, **Then** the user sees an error and can
   choose to wait or quit.
4. **Given** the user installs a plugin via the Plugin Manager,
   **When** the manager runs `git clone` into
   `~/.config/slopsmith-desktop/plugins/`, **Then** the plugin
   appears after restart.

---

### User Story 3 — VST/AU/LV2 plugin hosting + NAM + IR (Priority: P2)

A user with a guitar amp sim VST (Guitar Rig, Neural DSP, ToneX)
loads it inside the app. They can chain it with NAM for amp modelling
and a cabinet IR for speaker simulation. The signal chain is
re-orderable.

**Why this priority**: Differentiates Slopsmith Desktop from any
browser-only tool.

**Independent Test**: Open the signal-chain UI, add a VST, add NAM
with a `.nam` model, add an IR loader with a cab `.wav`, reorder,
play, hear the chain effect.

**Acceptance Scenarios**:

1. **Given** the user adds a VST, **When** `VSTHost::load(path)`
   succeeds, **Then** the plugin appears in the chain and processes
   audio.
2. **Given** the user loads a `.nam` file, **When**
   `NAMProcessor::load(path)` succeeds, **Then** the model is
   active in the chain.
3. **Given** the user loads an IR, **When** `IRLoader::load(file)`
   succeeds, **Then** convolution runs after NAM in the chain.
4. **Given** processors are reordered in the UI, **When** the
   reorder commits, **Then** `SignalChain::reorder(...)` reflects it
   on the audio thread without dropouts.

---

### User Story 4 — Bundled offline-capable install (Priority: P2)

A user with intermittent / no internet runs the installer and gets
a fully working app: bundled Python, bundled Slopsmith, bundled
default IRs, bundled GM soundfont, bundled `rscli`.

**Why this priority**: First-run reliability + lets users run the
app on isolated networks (studios, schools).

**Independent Test**: Install on an offline machine. Launch. Confirm
splash + library + audio engine + at least one default IR present.

**Acceptance Scenarios**:

1. **Given** electron-builder ran with `extraResources`, **When**
   the user installs the app, **Then** `resources/{slopsmith,python,
   bin,default-irs,soundfonts}` exist on disk.
2. **Given** the app launches offline, **When** the Python
   subprocess starts, **Then** it uses the bundled interpreter and
   Slopsmith source tree.

---

### User Story 5 — Reproducible Linux builds (Priority: P3)

A maintainer rebuilds the Linux distribution with the same toolchain
GitHub Actions uses, getting bit-similar artifacts on a fresh
checkout.

**Why this priority**: Required to debug Linux-only build
regressions and to support contributors.

**Independent Test**: From a clean checkout on a developer host,
run `./scripts/build-linux-release.sh`. Confirm `.AppImage` + `.deb`
land in `./release/` matching CI's outputs.

**Acceptance Scenarios**:

1. **Given** Docker is available + `../slopsmith/` exists, **When**
   the script runs, **Then** the DevContainer image builds and
   produces release artifacts.
2. **Given** the developer uses VS Code "Reopen in Container",
   **When** they run `npm run dist:linux`, **Then** the same
   pipeline runs.

---

### Edge Cases

- Native addon present but JUCE fails to enumerate any device:
  Settings shows an empty list and a clear "no devices" message.
- Python subprocess crashes mid-session: main process logs and
  attempts a single restart; if it fails again, surface error to
  user. [NEEDS CLARIFICATION: is auto-restart implemented today?]
- User installs a plugin via Plugin Manager that has a bad
  `requirements.txt`: install reports failure cleanly without
  corrupting the plugin dir.
- Quitting during startup: `appQuitting` flag breaks the polling
  loop early instead of waiting for the 5-minute deadline (see
  `src/main/main.ts`).
- Bundled Slopsmith path missing in dev: `python.ts` falls back to
  `~/Repositories/slopsmith/`; if that is also missing, startup
  fails with a clear message.

## Requirements

### Functional Requirements

- **FR-001**: App MUST embed Slopsmith UI in a webview pointed at
  `http://127.0.0.1:<port>/` where the Python subprocess is
  listening.
- **FR-002**: App MUST start its embedded Slopsmith server on a port
  ≥ 18000 to avoid Docker conflicts (`src/main/python.ts`).
- **FR-003**: App MUST load `slopsmith_audio.node` from one of three
  candidate paths (`src/main/audio-bridge.ts`); if all fail, audio
  features MUST be disabled gracefully.
- **FR-004**: App MUST expose audio device enumeration, selection,
  start/stop, gain, and pitch detection via N-API
  (`src/audio/NodeAddon.cpp`) and IPC channels in
  `src/main/ipc-channels.ts`.
- **FR-005**: App MUST host VST3 on all platforms, AU on macOS, LV2
  on Linux.
- **FR-006**: App MUST host NAM models when
  `src/audio/third_party/NAM/` is present at build time.
- **FR-007**: App MUST load impulse responses for cab simulation
  (`IRLoader`).
- **FR-008**: App MUST poll `/api/startup-status` every 700 ms with
  a 5-minute deadline and surface plugin-load progress on the
  splash screen.
- **FR-009**: App MUST manage plugins via git clone/pull in
  `~/.config/slopsmith-desktop/plugins/`.
- **FR-010**: App MUST package via electron-builder for
  AppImage+deb, dmg+zip, NSIS .exe.
- **FR-011**: App MUST set `asarUnpack` for `build/Release/*.node`
  so the native addon is loadable from the packaged app.
- **FR-012**: Linux distribution builds MUST be reproducible via
  the DevContainer / `scripts/build-linux-release.sh`.
- **FR-013**: App MUST not crash on unhandled rejections /
  exceptions in the main process; it MUST log and continue
  (`src/main/main.ts`).
- **FR-014**: App MUST support runtime Slopsmith path discovery
  for development (`../slopsmith/` or
  `~/Repositories/slopsmith/`) and bundled-resources mode for
  packaged installs.
- **FR-015**: App MUST allow the user to monitor mute (output
  silenced, but pitch detection still active) and to noise-gate the
  input post-input-gain pre-FX.
- **FR-016**: App MUST support multi-channel input device
  selection (e.g. Valeton GP-5 dry/wet split via `selectedInput
  Channel`).

### Key Entities

- **Audio Engine**: `AudioEngine` (C++/JUCE) — owns device manager,
  signal chain, pitch detector, backing-track player, noise gate,
  monitor mute.
- **Signal Chain**: ordered list of `SignalProcessor*` (VST/NAM/IR)
  with thread-safe reorder.
- **Pitch Detector**: emits frequency / note frames consumed by the
  renderer.
- **Python Subprocess**: bundled Slopsmith server.py running on
  port 18000+.
- **Plugin Manager**: git-clone-based installer for Slopsmith
  plugins.
- **Splash + Startup Status**: `StartupStatus` shape from
  `/api/startup-status`.

## Success Criteria

- **SC-001**: First boot to library browser within 30 s on a warm
  install.
- **SC-002**: Round-trip audio latency at 256-sample buffer @ 48 kHz
  ≤ 12 ms on supported interfaces.
- **SC-003**: All three OS distributions ship from the same source
  every release; no platform regresses for ≥ one release at a time.
- **SC-004**: Plugin install/uninstall is idempotent — repeated
  installs of the same plugin do not corrupt state.
- **SC-005**: Native addon load failure does not prevent the user
  from browsing their library or running plugins that don't need
  audio.

## Assumptions

- User has an audio interface compatible with the chosen backend.
- For VST hosting on Mac, the user has granted the app the
  `audio-input` and `automation` entitlements
  (see `resources/entitlements.mac.plist`).
- For NAM, the user supplies their own `.nam` files (we ship
  default IRs but not models).
- For Slopsmith plugins, the user trusts the upstream plugin repos
  they install from.
- For Linux reproducible builds, the DevContainer image matches
  the CI environment as long as both pin `ubuntu-22.04`.
