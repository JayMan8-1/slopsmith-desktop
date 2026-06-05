#!/usr/bin/env bash
# Pre-response checklist for GitHub PR review rounds.
# No GitHub API — reminders only.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REPO_NAME="$(basename "$REPO_ROOT")"

echo "=============================================="
echo " Slopsmith PR review helper"
echo " Repo: $REPO_NAME"
echo "=============================================="
echo ""
echo "Before responding to review comments:"
echo ""
echo "  [ ] git status          — know what's dirty"
echo "  [ ] git pull            — latest base + remote branch"
echo "  [ ] Read ALL comments   — classify before coding"
echo "  [ ] Paste into Cursor   — docs/ai/CURSOR_PROMPT_TEMPLATES.md"
echo "                         → 'GitHub review triage' first"
echo ""
echo "While fixing:"
echo ""
echo "  [ ] Surgical diff only  — no drive-by refactors"
echo "  [ ] Respect locked systems (see .cursor/rules/)"
echo "  [ ] Blockers/bugs first — nits batched last"
echo ""
echo "Test (pick what applies):"
echo ""
if [[ "$REPO_NAME" == "slopsmith-desktop" ]]; then
  echo "  [ ] npm run build:ts"
  echo "  [ ] npm run build:audio    (if native audio touched)"
  echo "  [ ] npm start              (smoke test)"
else
  echo "  [ ] npm run test:js        (if JS touched)"
  echo "  [ ] npm run test           (Playwright, if UI touched)"
  echo "  [ ] Manual player smoke    (load song, exercise change)"
fi
echo ""
echo "Ship:"
echo ""
echo "  [ ] git diff              — self-review"
echo "  [ ] git commit            — clear message"
echo "  [ ] git push"
echo "  [ ] Draft GitHub reply    — docs/ai/CURSOR_PROMPT_TEMPLATES.md"
echo "                         → 'PR reply draft'"
echo ""
echo "Docs:"
echo "  $REPO_ROOT/docs/ai/AI_CONTRIBUTOR_WORKFLOW.md"
echo "  $REPO_ROOT/docs/ai/PR_REVIEW_PLAYBOOK.md"
echo ""
echo "=============================================="
