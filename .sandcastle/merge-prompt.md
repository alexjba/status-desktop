# TASK

Land the following branches autonomously via fork-internal pull requests:

{{BRANCHES}}

Their corresponding issues:

{{ISSUES}}

# SETUP

Run `gh auth setup-git` once so `git push` works over HTTPS.

# PER BRANCH

For each branch, in order:

1. **Rebase on latest master** so the PR is current: `git fetch origin && git rebase origin/master <branch>` (work on the branch via `git checkout <branch>`). If the rebase conflicts, resolve intelligently by reading both sides; after resolving, verify the resolution by re-reading the merged result (at most targeted single-file checks like `nim check` on a touched module) before continuing.

   **NEVER run repo builds or test suites in this phase** — no `make`, no `make statusq`, no `make tests-nim-linux`, no status-go builds. You operate on the HOST's bind-mounted checkout: binaries produced by your Linux toolchain (the vendored Nim compiler, StatusQ libs, status-go artifacts) overwrite the host's macOS binaries and break every build on the host machine until manually repaired. Build verification already happened in the implementer/reviewer sandboxes; your job is only rebase → push → PR → CI → merge. If a conflict is too gnarly to resolve confidently without running tests, do not guess: leave the branch unmerged, comment on the issue explaining the conflict, and move on.
2. **Push**: `git push -u origin <branch>`. If rejected as non-fast-forward (branch existed remotely from an earlier cycle), run `git pull --rebase origin <branch>` and push again — never force-push (it is blocked).
3. **Open a PR** against `master`: `gh pr create --base master --head <branch> --title "<concise title>" --body "..."`. The body must include `Closes #<issue-number>` (so the issue auto-closes on merge) and a short summary of what was done and how it was verified.
4. **Wait for CI**: `gh pr checks <pr-number> --watch --interval 30`. Give it up to ~15 minutes.
   - **All green** → merge: `gh pr merge <pr-number> --merge --delete-branch`.
   - **A check fails** → do NOT merge. Comment on the issue with the failing check name and a one-line diagnosis, leave the PR open for the human, and move to the next branch.
   - **Still pending after ~15 min** → enable auto-merge instead of blocking: `gh pr merge <pr-number> --auto --merge`, comment on the issue that auto-merge is armed, and move on.

# AFTER ALL BRANCHES

Sync the local checkout with what landed: `git checkout master && git fetch origin && git merge --ff-only origin/master`.

Do not close issues manually — `Closes #N` handles it on merge. Report per-branch outcomes (merged / left open + why) in your final summary.

Once every branch is handled, output <promise>COMPLETE</promise>.
