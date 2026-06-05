# PR review playbook

Short rules for handling GitHub feedback without burning time or scope.

---

## Mindset

- **Do not take criticism personally.** Reviewers want a mergeable, maintainable PR.
- **The PR is not you.** Separate ego from the diff.
- **Silence is worse than a wrong fix.** If you disagree, say why politely.

---

## Before you write code

1. **Read every comment once** — no reactive single-comment fixes.
2. **Classify** each item (see [AI_CONTRIBUTOR_WORKFLOW.md](./AI_CONTRIBUTOR_WORKFLOW.md)).
3. **Ask clarification** if the comment is vague — one GitHub reply beats three bad commits.
4. **Fix blockers and bugs first** — nits last.
5. **Batch small nits** — one commit "address review nits" is fine.

---

## Scope control

- **Do not expand PR scope** because a reviewer mentioned a cool idea.
- Tag cool ideas as **future work** in your reply.
- If a blocker requires a bigger change, **say so on the PR** before doing it — reviewers may prefer a follow-up PR.

---

## Architecture comments

- If they say "this won't scale" or "wrong layer" — **stop and think**.
- Do not patch around architecture with hacks.
- Options: minimal fix in the right layer, or split PR, or discuss tradeoffs in the thread.

---

## When to push back (politely)

- Comment conflicts with locked V1 systems
- Requested change breaks gameplay sync or audio contract
- Nit demands a repo-wide rename unrelated to your PR
- Suggestion is factually wrong — explain with evidence

Example:

> Good catch on X. I left transport untouched here because that's locked for V1 — happy to open a follow-up if we want to revisit routing.

---

## After you fix

1. Run the right tests (see workflow doc).
2. **Self-review the diff** — `git diff main...HEAD`
3. Push
4. **Reply on GitHub** with:
   - what changed (per theme, not per arbitrary commit)
   - how you tested
   - what you deferred

---

## Reply checklist

- [ ] Every blocker addressed or explained
- [ ] Bugs fixed or reproduction requested
- [ ] Nits done or politely declined with reason
- [ ] No secrets in commits
- [ ] CI re-run or will re-run on push

---

## Cursor shortcut

1. Paste comments → **GitHub review triage** template
2. Approve the plan
3. **Safe Cursor patch** per fix batch
4. **PR reply draft** before you post on GitHub

Run `./scripts/dev/review-helper.sh` if you want a printed checklist.
