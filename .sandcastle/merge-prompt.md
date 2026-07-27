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
4. **Arm auto-merge immediately**: `gh pr merge <pr-number> --auto --merge --delete-branch`. Do NOT use `gh pr checks --watch` or any other long-blocking wait — you have ONE iteration; a blocking CI watch has previously eaten the whole budget, leaving PRs unmerged and later iterations spinning on already-complete branches. Move straight on to the next branch.

# AFTER ALL BRANCHES

Once every branch has a PR with auto-merge armed, do ONE bounded confirmation pass (the fork's checks normally finish in under a minute): poll `gh pr view <pr-number> --json state,mergeStateStatus` for each PR, at most ~6 short polls total (`sleep 20` between polls, never a `--watch`).
- **MERGED** → done for that branch.
- **A check failed** (auto-merge won't fire) → do NOT force anything. Comment on the issue with the failing check name and a one-line diagnosis and leave the PR open for the human.
- **Still pending when polls run out** → leave auto-merge armed, comment on the issue that it will land on green, and finish up — do not keep waiting.

Sync the local checkout with what landed and leave the HOST checkout the way you found it: `git checkout master && git fetch origin && git merge --ff-only origin/master`. If you stashed host-local changes at the start, restore them now with `git stash pop` — never finish with the checkout on a work branch or with the host's dirty files still stashed.

Do not close issues manually — `Closes #N` handles it on merge. Report per-branch outcomes (merged / left open + why) in your final summary.

Once every branch is handled, output <promise>COMPLETE</promise>.
