# Cursor prompt templates

Copy a block into Cursor and fill the placeholders. Do not skip **DO NOT TOUCH** on Slopsmith PRs.

---

## GitHub review triage

```text
GOAL: Classify each GitHub review comment and recommend fix vs reply-only.

CONTEXT:
- Repo: [slopsmith | slopsmith-desktop]
- PR: [link or number]
- Branch: [branch name]
- My change summary: [one sentence]

COMMENTS (paste verbatim):
1. ...
2. ...

DO NOT TOUCH:
- Locked systems unless a comment is an approved exception: transport, bridge, gameplay sync, websocket, arrangement loading, playback routing, SoundTouch V1 DSP, preset/snap UX

SUCCESS CRITERIA:
- Table: # | category | action (fix now / batch nit / defer / ask) | risk | files likely touched

TEST COMMANDS:
- Suggest commands only; do not claim they ran.
```

---

## Safe Cursor patch

```text
GOAL: [one sentence — the smallest fix that satisfies the review]

CONTEXT:
- Repo: [slopsmith | slopsmith-desktop]
- PR comment(s) addressed: [quote or summarize]
- Relevant files: [paths if known]

FILES:
- [path/to/file.js]
- (or: "find the code that handles X")

DO NOT TOUCH:
- [list locked systems]
- No unrelated refactors

SUCCESS CRITERIA:
- [ ] [observable behavior fixed]
- [ ] No change to [sync / audio / API you must preserve]
- [ ] Diff stays under ~[N] lines if possible

TEST COMMANDS:
- cd [repo] && [commands]
- Manual: [steps]
```

---

## CI failure fix

```text
GOAL: Fix the CI failure with the smallest change.

CONTEXT:
- Repo: [slopsmith | slopsmith-desktop]
- PR: [number]
- Failing job: [name]
- Failing step log (paste):
```
[paste log]
```

FILES:
- [if log points to files; else "infer from log"]

DO NOT TOUCH:
- Locked systems unless CI proves they are the root cause
- Do not disable tests or skip hooks to greenwash CI

SUCCESS CRITERIA:
- [ ] Identified root cause in one paragraph
- [ ] Fix is surgical
- [ ] Same failure would be caught locally by TEST COMMANDS

TEST COMMANDS:
- [exact command CI runs, e.g. npm run test:js]
```

---

## PR reply draft

```text
GOAL: Draft a professional GitHub PR reply to reviewers.

CONTEXT:
- Comments addressed: [list]
- Comments deferred: [list]
- Commits: [short list or "single commit abc123"]

FILES:
- [what changed]

DO NOT TOUCH:
- N/A (reply only — no code)

SUCCESS CRITERIA:
- Tone: calm, specific, no defensiveness
- Each review point acknowledged
- States how tested
- Mentions anything intentionally not done

TEST COMMANDS:
- [what I ran — for inclusion in the reply]
```

---

## Small feature implementation

```text
GOAL: [feature in one sentence]

CONTEXT:
- User story: [who needs what]
- Existing code to extend: [files/patterns]

FILES:
- [expected touch points]

DO NOT TOUCH:
- Locked systems: [list]
- No scope creep into [related feature]

SUCCESS CRITERIA:
- [ ] [acceptance criterion 1]
- [ ] [acceptance criterion 2]

TEST COMMANDS:
- [repo-specific commands]
- Manual: [steps]
```

---

## Regression fix

```text
GOAL: Fix regression: [symptom]

CONTEXT:
- Reported by: [reviewer / user / CI]
- Last known good: [version or PR if known]
- Repro steps:
  1. ...
  2. ...

FILES:
- [suspected files]

DO NOT TOUCH:
- Locked systems unless proven root cause
- Do not "fix" by disabling the feature

SUCCESS CRITERIA:
- [ ] Repro no longer occurs
- [ ] No new failure in TEST COMMANDS
- [ ] Root cause explained in summary

TEST COMMANDS:
- [commands + manual repro verification]
```

---

## Documentation update

```text
GOAL: Update docs for [topic]

CONTEXT:
- Audience: [contributors | musicians | API users]
- Trigger: [review comment / feature ship / drift]

FILES:
- docs/...

DO NOT TOUCH:
- Product code (docs only unless a comment/doc mismatch requires a one-line code comment)

SUCCESS CRITERIA:
- [ ] Accurate vs current behavior
- [ ] Plain English
- [ ] Links to related docs if any

TEST COMMANDS:
- N/A — proofread only
- Optional: verify commands in doc actually run
```
