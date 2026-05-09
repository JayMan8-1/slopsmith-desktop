# Cross-Artifact Consistency Report

## Coverage

| Spec FR | Constitution | Plan | Tasks |
|---|---|---|---|
| FR-001 webview at localhost | I, III | Process layout | T030, T032 |
| FR-002 port ≥ 18000 | III | Architecture | T030 |
| FR-003 native addon load fallback | VII | Audio bridge / Design decisions | T024 |
| FR-004 audio device IPC | II, III | Architecture | T020, T023, T024 |
| FR-005 VST3/AU/LV2 hosting | II, VIII | Audio signal flow | T040 |
| FR-006 NAM hosting | II | Audio signal flow | T041 |
| FR-007 IR loading | II | Audio signal flow | T042 |
| FR-008 splash poll 700 ms / 5 min | III | Startup flow | T032 |
| FR-009 plugin manager | VI | Architecture | T034 |
| FR-010 electron-builder per-OS | IV, VIII | Design decisions | T005, T070–T073 |
| FR-011 asarUnpack | II, IV | Design decisions | T054 |
| FR-012 reproducible Linux | V | Architecture / Reproducible | T060–T063 |
| FR-013 main process resilience | VII | Startup flow | T010 |
| FR-014 Slopsmith path search | I, IV | Design decisions | T033 |
| FR-015 monitor mute + noise gate | II | Audio signal flow | T022, T025 |
| FR-016 multi-channel input | II | Constraints | T026 |

All FRs map to constitution principles, plan sections, and tasks.

## Drift

- **README architecture diagram vs plan**: README ASCII diagram
  describes the same three-process model; consistent.
- **Constitution VIII vs README support table**: README claims
  Windows 10+, macOS 12+, Linux, with per-OS audio backends
  matching the table. Consistent.
- **package.json `dist:*` scripts vs scripts/build-*.sh**: dist
  scripts run `electron-builder` after `build:native + bundle +
  build:ts`, while `scripts/build-*.sh` are the bundle-/release-
  oriented helpers. Two paths, both valid; the README points
  contributors at the right one for distribution.

## Gaps

1. **No automated tests (T027).** The audio engine and the
   pitch detector are exactly the kind of code that benefits from
   targeted unit tests. Today, regressions are found at integration
   time.
2. **Python subprocess restart policy (T036, clarify Q4).** If
   the Python child crashes, the documented behaviour is unclear.
   Either implement and document an auto-restart-once policy, or
   document explicitly that the user is expected to relaunch.
3. **Plugin auto-update policy (T037).** Constitution VI says app
   updates don't touch plugins, but how plugin updates surface in
   the UI / how often the manager checks isn't pinned down.
4. **CI workflow visibility (T065).** Constitution V cites CI's
   `ubuntu-22.04` runner as the reproducibility anchor, but no
   `.github/workflows/*` file was inspected here.
5. **Per-OS pre-release verification checklist (T074).** Cross-
   platform parity is non-negotiable per Constitution VIII; a
   short pre-release manual checklist (audio devices enumerable,
   one VST loads, one IR loads, one NAM model loads on each OS)
   would make this auditable.
6. **Runtime telemetry / crash reports.** Constitution VII keeps
   the app alive on errors; there is no mechanism documented for
   collecting those errors so they can be fixed.

## Recommendations

1. **Add a `tests/audio/` directory** with at least: pitch detector
   on synthesised input; signal chain reorder under simulated
   audio thread; NAM processor with a tiny stub model.
2. **Document subprocess restart policy** in `python.ts` header
   comment AND in the constitution. Likely "single retry on
   non-clean exit; surface error after second crash."
3. **Document plugin manager update flow** — when does it fetch?
   manual button vs auto-poll? Either way, write it down.
4. **Link CI workflow** from `docs/BUILD_ARCHITECTURE.md` and
   confirm pinning to `ubuntu-22.04` matches the DevContainer base.
5. **Add a `RELEASE_CHECKLIST.md`** with per-OS smoke tests.
6. **Optional**: opt-in crash reporter (Sentry-style or a local
   log dump the user can submit) for Electron main + renderer.
   Native addon crashes are harder; at minimum record `audio:
   isAvailable === false` and the failed candidate paths.
7. **Tighten plugin discovery contract** between Slopsmith server
   and `plugin-manager.ts`, so a plugin missing a `requirements.txt`
   or a `plugin.json` fails predictably.
