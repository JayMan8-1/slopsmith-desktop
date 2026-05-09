# Tasks: Slopsmith Desktop App

**Input**: Retrospective documentation of the existing implementation.
**Organization**: Tasks grouped by user story. **DONE** = present in
the repo with file pointer; **OPEN** = real gap.

## Phase 1: Setup

- [x] **DONE** T001 Electron + TypeScript project scaffold
  (`package.json`, `tsconfig.json`)
- [x] **DONE** T002 cmake-js audio addon scaffold (`CMakeLists.txt`,
  `src/audio/CMakeLists.txt`)
- [x] **DONE** T003 JUCE submodule
- [x] **DONE** T004 [P] Bundled NeuralAmpModelerCore at
  `src/audio/third_party/NAM/`
- [x] **DONE** T005 [P] electron-builder config in
  `package.json.build` for linux/mac/win

## Phase 2: Foundational

- [x] **DONE** T010 Electron main entry (`src/main/main.ts`) with
  uncaughtException / unhandledRejection swallow handlers
- [x] **DONE** T011 IPC channel constants (`src/main/ipc-channels.ts`)
- [x] **DONE** T012 Renderer preload via contextBridge
  (`src/main/preload.ts`)
- [x] **DONE** T013 Splash window + JSON spinner asset
  (`src/main/splash.html`, `splash-preload.ts`, `spinner.json`)
- [x] **DONE** T014 Resource extraction layout in
  `package.json.build.extraResources` (slopsmith, python, bin,
  default-irs, soundfonts)
- [x] **DONE** T015 Bundle scripts (`scripts/bundle-{slopsmith,
  python,binaries,soundfont}.sh`, `scripts/bundle.sh`)

## Phase 3: User Story 1 — Plug in and play (P1)

- [x] **DONE** T020 `AudioEngine` (JUCE) device manager,
  start/stop, gains — `src/audio/AudioEngine.{cpp,h}`
- [x] **DONE** T021 [P] `PitchDetector` (YIN) —
  `src/audio/PitchDetector.{cpp,h}`
- [x] **DONE** T022 [P] `NoiseGate` —
  `src/audio/NoiseGate.{cpp,h}`
- [x] **DONE** T023 N-API surface — `src/audio/NodeAddon.cpp`
- [x] **DONE** T024 Audio bridge (Electron side) with
  candidate-path addon load and `audio:isAvailable` —
  `src/main/audio-bridge.ts`
- [x] **DONE** T025 [P] Settings UI for device/sr/buffer +
  monitor mute + noise gate — `src/renderer/settings.html`
- [x] **DONE** T026 [P] Multi-channel input selection
  (`setInputChannel`) — `src/audio/AudioEngine.h`
- [ ] **OPEN** T027 Audio engine unit tests (no tests dir present
  today). High-value because audio bugs are hardest to detect at
  the integration layer.

**Checkpoint**: P1 ships — guitar in, sound out, pitch up.

## Phase 4: User Story 2 — Embedded Slopsmith UI + plugin manager (P1)

- [x] **DONE** T030 Python subprocess supervisor with port-≥-18000
  selection — `src/main/python.ts`
- [x] **DONE** T031 [P] `getStartupStatus` polling shape +
  `RawStartupStatus` validator — `src/main/python.ts`
- [x] **DONE** T032 Splash poll loop with 700 ms interval and
  300 000 ms deadline — `src/main/main.ts`
- [x] **DONE** T033 Slopsmith path search (extraResources, ../slopsmith,
  ~/Repositories/slopsmith) — `src/main/python.ts`
- [x] **DONE** T034 Plugin Manager (git clone/pull, list,
  remove) — `src/main/plugin-manager.ts`,
  `src/renderer/plugin-manager/`
- [x] **DONE** T035 [P] Soundfont manager —
  `src/main/soundfont-manager.ts`
- [ ] **OPEN** T036 Documented Python-subprocess auto-restart
  policy (or a clear "we don't auto-restart" statement) — see
  clarify.md.
- [ ] **OPEN** T037 [P] Plugin auto-update cadence + UI prompt
  policy explicitly documented.

**Checkpoint**: P1 also ships — full Slopsmith UI + plugin
ecosystem inside the desktop app.

## Phase 5: User Story 3 — VST/AU/LV2 + NAM + IR (P2)

- [x] **DONE** T040 `VSTHost` (VST3 everywhere; AU on Mac; LV2 on
  Linux) — `src/audio/VSTHost.{cpp,h}`
- [x] **DONE** T041 [P] `NAMProcessor` —
  `src/audio/NAMProcessor.{cpp,h}` (gated on
  `src/audio/third_party/NAM/CMakeLists.txt` presence in CMake)
- [x] **DONE** T042 [P] `IRLoader` (convolution) —
  `src/audio/IRLoader.{cpp,h}`
- [x] **DONE** T043 `SignalChain` ordered processor list with
  thread-safe reorder — `src/audio/SignalChain.{cpp,h}`
- [x] **DONE** T044 [P] Default IRs bundled — `resources/default-irs/`
- [ ] **OPEN** T045 Signal-chain UI screenshot / docs in
  `docs/BUILD_ARCHITECTURE.md`-style file (currently only the
  diagram in README).

**Checkpoint**: P2 ships.

## Phase 6: User Story 4 — Bundled offline-capable install (P2)

- [x] **DONE** T050 Bundled Python interpreter —
  `resources/python/`, `scripts/bundle-python.sh`
- [x] **DONE** T051 [P] Bundled Slopsmith source —
  `resources/slopsmith/`, `scripts/bundle-slopsmith.sh`
- [x] **DONE** T052 [P] Bundled `rscli` —
  `resources/bin/`, `scripts/bundle-binaries.sh`,
  `scripts/build-rscli.sh`
- [x] **DONE** T053 [P] Bundled GM soundfont —
  `resources/soundfonts/`, `scripts/bundle-soundfont.sh`
- [x] **DONE** T054 [P] `asarUnpack: ["build/Release/*.node"]` —
  `package.json.build`

**Checkpoint**: P2 ships.

## Phase 7: User Story 5 — Reproducible Linux builds (P3)

- [x] **DONE** T060 Linux Docker build script —
  `scripts/build-linux-docker.sh`
- [x] **DONE** T061 [P] Linux Ubuntu host build script —
  `scripts/build-linux-ubuntu.sh`
- [x] **DONE** T062 [P] Top-level `build-linux-release.sh` runner
- [x] **DONE** T063 [P] DevContainer doc references in README
- [x] **DONE** T064 Build architecture doc —
  `docs/BUILD_ARCHITECTURE.md`
- [ ] **OPEN** T065 GitHub Actions workflow that exercises the
  same scripts on `ubuntu-22.04` (assumed to exist via CI; not
  visible in this checkout). Confirm and link from
  `BUILD_ARCHITECTURE.md`.

**Checkpoint**: P3 ships.

## Phase 8: Cross-platform polish (Constitution VIII)

- [x] **DONE** T070 macOS sign / entitlements —
  `scripts/sign-macos-binaries.sh`,
  `resources/entitlements.mac.plist`
- [x] **DONE** T071 [P] macOS build script —
  `scripts/build-macos.sh`
- [x] **DONE** T072 [P] Windows build script —
  `scripts/build-windows.sh`
- [x] **DONE** T073 [P] Windows build requirements doc —
  `WINDOWS_BUILD_REQUIREMENTS.md`
- [ ] **OPEN** T074 [P] Documented per-OS audio backend test
  matrix (we currently rely on the README table; a CONTRIBUTING-
  style "how to verify before release" checklist would help).

## Parallel-Safe Sets

- T021, T022, T040, T041, T042 are independent C++ files; safe
  to refactor / test in parallel.
- T071, T072 are different OS scripts.
- T050 – T054 are independent bundle scripts.
