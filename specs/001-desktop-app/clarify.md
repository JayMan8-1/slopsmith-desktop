# Clarifications: Slopsmith Desktop App

### Q: Why port 18000+ for the embedded server, not 8000?

**A:** A user often runs Slopsmith inside Docker on 8000 already. Picking
a base above 18000 lets the desktop app coexist on the same machine
without surprising port collisions. See `src/main/python.ts`
(`serverPort = 18000`).

### Q: How does the app find the Slopsmith source tree?

**A:** Three-tier search: (1) packaged extraResources at
`process.resourcesPath/slopsmith` for installs; (2) `../slopsmith/`
for dev when run from a sibling checkout; (3)
`~/Repositories/slopsmith/` as a final fallback. If none exist,
startup fails with an actionable error.

### Q: What happens if the native audio addon fails to load?

**A:** `src/main/audio-bridge.ts` swallows the load error, sets
`audio = null`, and `audio:isAvailable` returns `false`. The app
continues running so users can still browse, edit, and use plugins
that don't need audio. Constitution Principle VII.

### Q: Is the Python subprocess auto-restarted on crash?

**A:** [OPEN] — the file `src/main/python.ts` was only partially
read here. The repo currently has unhandled-rejection / uncaught-
exception handlers in `main.ts` that log and continue, but a
documented "subprocess died → restart once" loop is not visible.
Worth confirming in the source.

### Q: Where do user plugins live?

**A:** `~/.config/slopsmith-desktop/plugins/` (Linux). Equivalent
`app.getPath('userData') + '/plugins'` on Mac/Windows. Managed by
`src/main/plugin-manager.ts`.

### Q: Why is `electron-builder.asarUnpack: ["build/Release/*.node"]`
required?

**A:** Native modules cannot be `require()`'d from inside an
asar archive on all platforms. Unpacking the `.node` files lets the
audio bridge `require()` them at runtime.

### Q: Is NAM optional at build time?

**A:** Yes. `CMakeLists.txt` checks for
`src/audio/third_party/NAM/CMakeLists.txt` and disables NAM if
absent. Distribution builds are expected to ship with NAM enabled.

### Q: How does multi-channel input selection work for interfaces
like Valeton GP-5?

**A:** `AudioEngine::setInputChannel(int)` takes 0 (left/dry),
1 (right/wet), or -1 (mono mix). Exposed through the audio bridge.

### Q: Is there any tests directory?

**A:** [OPEN] — none observed at the top level. Audio engine code
in `src/audio/` is a strong candidate for JUCE-based unit tests
that don't require a device.

### Q: What's the upgrade story for installed plugins when the
desktop app updates?

**A:** Constitution Principle VI: app updates do not touch the
plugin dir. Plugin updates are user-initiated via the Plugin
Manager (a git pull). [OPEN] — auto-update prompts in the Plugin
Manager UI exist but their cadence/policy aren't fully documented
here.

### Q: Why ship a bundled Python interpreter?

**A:** Lets the app run without the user installing Python and
matching versions. It also pins exactly the Python that
Slopsmith's `requirements.txt` was tested against.
