# AI contributor workflow (Joe)

Lightweight loop for GitHub PR comments, CI failures, and Cursor patches — without inventing a new prompt every time.

**Not a product feature.** Developer infrastructure only.

Repos:

| Repo | What lives here |
|------|-----------------|
| **slopsmith** | Static player, server UI, plugins, docs |
| **slopsmith-desktop** | Electron, JUCE audio, native addon |

Use the repo that matches your PR. Cursor rules in `.cursor/rules/` apply per repo.

---

## The loop (repeat every review round)

1. **Pull latest** on your branch
2. **Paste** GitHub comments into Cursor (use [CURSOR_PROMPT_TEMPLATES.md](./CURSOR_PROMPT_TEMPLATES.md) → *GitHub review triage*)
3. **Classify** each comment before coding
4. **Generate** a safe patch prompt (*safe Cursor patch* template)
5. **Apply** the fix in Cursor — surgical diff only
6. **Run tests** (see below)
7. **Commit** with a clear message
8. **Push** and **reply** on GitHub (*PR reply draft* template)

---

## How to paste GitHub comments into Cursor

Copy the reviewer text exactly. Include:

- file/line if they cited one
- the full thread if context matters
- which repo/PR (slopsmith vs slopsmith-desktop)

Example opener:

```text
GitHub PR review — classify and propose a surgical fix.

PR: byrongamatos/slopsmith#123
Branch: fix/loop-marker-sync

Comments:
1. [reviewer] "This will desync gameplay if you seek here..."
2. [reviewer] "nit: rename foo to bar"
```

Then use the triage template from `CURSOR_PROMPT_TEMPLATES.md`.

---

## Classify before you code

| Category | Action |
|----------|--------|
| **blocker** | Must fix before merge |
| **bug** | Fix if reproducible; ask for steps if not |
| **architecture concern** | Do not hack around it — propose minimal fix or discuss |
| **style/nit** | Batch with other nits; one small commit |
| **documentation** | Docs-only change; no drive-by code |
| **future work** | Reply acknowledging; do not expand PR scope |
| **unclear** | Ask clarification; do not guess |

---

## Generate a safe patch prompt

After triage, fill the **safe Cursor patch** template:

- narrow **GOAL**
- list **FILES** if known
- explicit **DO NOT TOUCH** (locked systems)
- **SUCCESS CRITERIA** you can verify
- **TEST COMMANDS**

Cursor project rules (`.cursor/rules/slopsmith-contributor-workflow.mdc`) reinforce this automatically.

---

## How to test

Pick what matches your change:

### slopsmith

```bash
cd slopsmith
npm run test:js          # static/app.js, plugins, contract tests
npm run test             # Playwright (needs server running if integration)
```

Manual smoke: load a song, hit the feature you changed, watch the browser console.

### slopsmith-desktop

```bash
cd slopsmith-desktop
npm run build:ts         # TypeScript main/preload
npm run build:audio      # only if native audio changed
npm start
```

Audio/player changes: test in the desktop app, not only unit-less builds.

### CI failed?

Paste the failing job log into Cursor using the **CI failure fix** template.

---

## How to commit

```bash
git status
git diff
git add <files>
git commit -m "fix(scope): short description"
git push
```

One logical change per commit when possible. Do not commit secrets (`.env`, keys).

---

## How to reply on GitHub (professional, short)

Structure:

1. **Thanks** — one line
2. **What you did** — bullet per comment or theme
3. **How you tested** — specific commands or manual steps
4. **What you did not do** — if you deferred (future work, out of scope)

Example:

> Thanks for the review.
>
> - Addressed the seek race by … (see `app.js` `seekTo`)
> - Renamed `foo` → `bar` in the loop panel only
>
> Tested: `npm run test:js`, manual loop wrap at 75% speed in desktop build.
>
> Did not change transport architecture — agreed that belongs in a follow-up PR.

Use the **PR reply draft** template for longer threads.

---

## Locked systems reminder

Do not touch these unless the PR **explicitly** requires it and reviewers agree:

- JUCE transport, Electron bridge, gameplay sync, websocket flow
- Arrangement loading, realtime playback routing
- SoundTouch V1 DSP, preset/snap slowdown UX

When in doubt, ask on the PR before coding.

---

## Helper script

```bash
./scripts/dev/review-helper.sh
```

Prints a pre-response checklist (no GitHub API).

---

## Related docs

- [CURSOR_PROMPT_TEMPLATES.md](./CURSOR_PROMPT_TEMPLATES.md) — copy/paste prompts
- [PR_REVIEW_PLAYBOOK.md](./PR_REVIEW_PLAYBOOK.md) — mindset and rules
