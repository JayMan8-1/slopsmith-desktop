# Implementation Plan: Slopsmith Desktop App

**Branch**: `001-desktop-app` (retrospective) | **Date**: 2026-05-09
**Spec**: [spec.md](./spec.md)

## Summary

An Electron + JUCE desktop wrapper around upstream Slopsmith. Three
processes: Electron renderer (embeds Slopsmith UI in a webview),
Electron main (window + IPC + audio bridge + plugin manager + Python
supervisor), and a Python subprocess running `slopsmith/server.py` on
port ≥ 18000. The audio engine is a JUCE C++ library compiled as a
Node.js native addon via cmake-js, providing VST/AU/LV2 hosting, NAM,
IR convolution, and YIN-based pitch detection. Distributable via
electron-builder for Windows, macOS, and Linux; Linux builds are
reproducible via the DevContainer.

## Technical Context

**Language/Version**:
- TypeScript (Electron main + renderer wiring) — Node 22+
- C++20 (JUCE audio engine)
- Python 3.12+ (Slopsmith subprocess)

**Primary Dependencies**: Electron, electron-builder, JUCE
(submodule), cmake-js, NeuralAmpModelerCore (vendored at
`src/audio/third_party/NAM/`), Slopsmith (sibling checkout or
bundled).
**Storage**: Filesystem only. User config under
`app.getPath('userData')`. Plugins under
`~/.config/slopsmith-desktop/plugins/`.
**Testing**: [NEEDS CLARIFICATION: no tests directory present.]
**Target Platform**: Windows 10+, macOS 12+, Linux (Ubuntu 22.04
baseline for distribution builds).
**Project Type**: Cross-platform desktop application (Electron + native
audio addon + Python subprocess).
**Performance Goals**: ≤ 12 ms round-trip audio at 256-sample @
48 kHz; first-launch to UI ≤ 30 s; splash polls every 700 ms with a
5-minute deadline.
**Constraints**: Must run offline after install; native addon load
failures must not crash the app; cross-platform parity is
non-negotiable.
**Scale/Scope**: Single-user desktop tool. Codebase ≈ 50 source files
across `src/audio/`, `src/main/`, `src/renderer/`. CMake project for
audio addon. Multiple bundle scripts in `scripts/`.

## Constitution Check

| Principle | Where it shows up |
|---|---|
| I. Wrap, don't fork | Bundled Slopsmith via `extraResources` in `package.json`; `src/main/python.ts` spawns upstream `server.py`. |
| II. Native audio is the whole point | `src/audio/*.cpp/h` (JUCE), `src/audio/NodeAddon.cpp` (N-API), cmake-js build (`scripts/build-audio.sh`). |
| III. Three-process architecture | `src/main/`, `src/renderer/`, plus Python subprocess via `src/main/python.ts`. |
| IV. Bundle everything | `package.json.build.extraResources` packages `slopsmith`, `python`, `bin`, `default-irs`, `soundfonts`. |
| V. Reproducible Linux builds | `.devcontainer/`, `scripts/build-linux-release.sh`, `scripts/build-linux-docker.sh`. |
| VI. Plugin isolation | `src/main/plugin-manager.ts` — git clones into user config dir. |
| VII. Fail soft on audio absence | `src/main/audio-bridge.ts` `loadNativeAddon()` returns `null` on failure; app continues. |
| VIII. Cross-platform means all three | `package.json.build.{linux,mac,win}`; `scripts/build-{linux,macos,windows}.sh`; per-OS audio backends in `src/audio/AudioEngine.cpp`. |

No deviations.

## Project Structure

```
slopsmith-desktop/
├── CMakeLists.txt              # Audio addon build
├── package.json                # Electron app + builder config
├── tsconfig.json
├── JUCE/                       # Submodule
├── src/
│   ├── main/                   # Electron main process (TS)
│   │   ├── main.ts             # Window lifecycle, splash, startup poll
│   │   ├── python.ts           # Python subprocess supervisor
│   │   ├── audio-bridge.ts     # IPC ↔ native addon
│   │   ├── plugin-manager.ts   # git-based plugin installer
│   │   ├── soundfont-manager.ts
│   │   ├── ipc-channels.ts     # IPC channel name constants
│   │   ├── preload.ts          # Renderer preload (contextBridge)
│   │   ├── splash.html / splash-preload.ts / spinner.json
│   │   └── images/             # Splash imagery
│   ├── audio/                  # JUCE C++ native addon
│   │   ├── AudioEngine.{cpp,h}
│   │   ├── SignalChain.{cpp,h}
│   │   ├── VSTHost.{cpp,h}
│   │   ├── NAMProcessor.{cpp,h}
│   │   ├── IRLoader.{cpp,h}
│   │   ├── PitchDetector.{cpp,h}  # YIN
│   │   ├── NoiseGate.{cpp,h}
│   │   ├── NodeAddon.cpp          # N-API surface
│   │   ├── third_party/NAM/       # NeuralAmpModelerCore
│   │   └── CMakeLists.txt
│   └── renderer/               # In-app non-Slopsmith UI
│       ├── settings.html
│       ├── plugin-manager/
│       ├── screen.{html,js}
│       └── plugin.json
├── resources/                  # extraResources for electron-builder
│   ├── slopsmith/              # Bundled Slopsmith snapshot
│   ├── python/                 # Bundled interpreter
│   ├── bin/                    # rscli, etc.
│   ├── default-irs/
│   ├── soundfonts/
│   ├── icons/
│   └── entitlements.mac.plist
├── scripts/                    # Build & bundle scripts
│   ├── setup-dev.sh
│   ├── build-audio.sh / build-rscli.sh
│   ├── build-{linux-{docker,ubuntu},macos,windows}.sh
│   ├── build-linux-release.sh / build-release.sh
│   ├── bundle-{slopsmith,python,binaries,soundfont}.sh
│   ├── bundle.sh / parse-build-config.py / sign-macos-binaries.sh
│   └── BUILD_SCRIPTS.md
├── docs/BUILD_ARCHITECTURE.md
├── WINDOWS_BUILD_REQUIREMENTS.md
├── CONTRIBUTORS.md
├── README.md / CLAUDE.md
├── build/                      # cmake-js output
├── dist/                       # tsc output
└── release/                    # electron-builder output
```

## Architecture & Data Flow

### Process layout

```
┌─────────────────────────────────────────────────────────────┐
│ Electron Main (Node 22, src/main/*.ts)                      │
│                                                             │
│ ┌────────────────┐  ┌────────────────┐  ┌────────────────┐  │
│ │ python.ts      │  │ audio-bridge   │  │ plugin-manager │  │
│ │ spawn server.py│  │ N-API ↔ IPC    │  │ git clone/pull │  │
│ │ port 18000+    │  │                │  │                │  │
│ └────────┬───────┘  └────────┬───────┘  └────────────────┘  │
│          │                   │                              │
│          │ child_process     │ require('slopsmith_audio')   │
│          ▼                   ▼                              │
│ ┌────────────────┐  ┌────────────────────────────────────┐  │
│ │ Python Subproc │  │ JUCE C++ Native Addon              │  │
│ │ slopsmith/     │  │ (build/Release/slopsmith_audio.    │  │
│ │ server.py      │  │  node)                             │  │
│ │ port 18000+    │  │                                    │  │
│ └────────────────┘  │  AudioEngine                       │  │
│                     │   ├ DeviceManager                  │  │
│                     │   ├ SignalChain                    │  │
│                     │   │   ├ VSTHost                    │  │
│                     │   │   ├ NAMProcessor               │  │
│                     │   │   └ IRLoader                   │  │
│                     │   ├ PitchDetector (YIN)            │  │
│                     │   ├ NoiseGate                      │  │
│                     │   └ Backing-track player           │  │
│                     └────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                ▲
                │  ipcRenderer ↔ ipcMain
                ▼
┌─────────────────────────────────────────────────────────────┐
│ Electron Renderer                                           │
│                                                             │
│  Webview → http://127.0.0.1:<pythonPort>/                   │
│  (Slopsmith full UI: library, highway, plugins)             │
│                                                             │
│  Native UI overlays (src/renderer/):                        │
│   ├ Settings (audio devices, gains, monitor mute, gate)     │
│   ├ Plugin Manager UI                                       │
│   └ Splash (during boot)                                    │
└─────────────────────────────────────────────────────────────┘
```

### Audio signal flow (real-time thread)

```
Guitar Input
   │
   ▼
[Input Gain]              ──→ [Pitch Detector] ──→ IPC tap to renderer
   │                            (YIN, sees ungated signal)
   ▼
[Noise Gate] (if enabled)
   │
   ▼
[Signal Chain]
   │   VST → NAM → IR (user-orderable)
   ▼
[Output Gain]
   │
   ▼
Speakers / Headphones
   ▲
   │
   └── [Backing track player] mixed in at backingVolume
```

`monitorMuted` silences the post-output-gain bus while leaving the
pitch detector / metering live (`AudioEngine::setMonitorMute`).

### Startup flow

```
electron app.whenReady()
   │
   ▼
Create splash window (src/main/splash.html)
   │
   ▼
initAudioBridge(mainWindow)         # may fail soft → audio = null
initSoundfontManager()
initPluginManager()
   │
   ▼
startPython()                       # picks port ≥ 18000
   │
   ▼
Poll /api/startup-status every 700 ms
   │
   ▼ (plugins finish or 5-min deadline)
Create main window, navigate to http://127.0.0.1:<port>/
   │
   ▼
Hide / close splash after SPLASH_CLOSE_DELAY_MS (300 ms)
```

## Design Decisions

### Native audio addon, not WebAudio

Sub-12 ms round-trip latency with VST/AU/LV2 hosting cannot be
achieved in WebAudio. JUCE gives us cross-platform device backends,
plugin hosting, and a known real-time audio model. The cost is a
cmake-js build dependency and per-OS toolchain pain — accepted.

### Python subprocess instead of porting Slopsmith to Node

Constitution Principle I — wrap, don't fork. Slopsmith's plugin
ecosystem is Python; rewriting in Node would fork the whole world.

### Splash deadline of 5 minutes, polling every 700 ms

5 minutes is generous enough to handle slow plugin pip-installs
on first launch; 700 ms is fast enough for a responsive progress
bar. The constants live in `src/main/main.ts` as
`STARTUP_DEADLINE_MS` / `STARTUP_POLL_INTERVAL_MS`.

### Audio bridge candidate-paths fallback

Three known locations for `slopsmith_audio.node` (dev, packaged
asarUnpack, packaged direct copy) — try them in order, log on
failure. Avoids different load logic for dev vs. packaged.

### `asarUnpack: ["build/Release/*.node"]`

Required for the addon to be `require()`-able from inside a packaged
app on macOS / Windows. Without it the app would only work in dev.

### Per-OS distribution targets in `package.json.build`

Linux: AppImage + deb. macOS: dmg + zip. Windows: NSIS exe (defaults
of electron-builder). Trust electron-builder for installer
mechanics; we handle code signing / notarization via
`scripts/sign-macos-binaries.sh` and `entitlements.mac.plist`.

### DevContainer for reproducible Linux builds

`ubuntu-22.04` GitHub Actions image is the production target; the
DevContainer pins to the same base so `dist:linux` produces a
binary that runs on the same set of glibc / libstdc++ versions.

## Slopsmith Ecosystem Integration

- **Upstream Slopsmith**: bundled into the installer; Constitution
  Principle I forbids forking.
- **Slopsmith Demucs Server**: optional. The user configures its
  URL in Slopsmith settings (renderer setting, not a desktop-app
  setting). The desktop app does not run a Demucs server itself.
- **Plugin repos**: cloned by `src/main/plugin-manager.ts` into the
  user config dir. Updates are user-initiated.
- **Slopsmith Demo**: unrelated; the desktop app is the install
  target the demo points users toward.
- **Slopsmith Ignition (The Slop Shop)**: also unrelated to the
  app process, but is the catalog users may visit to find sloppaks
  to drop into their library.
