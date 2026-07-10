# TASK

Fix issue {{TASK_ID}}: {{ISSUE_TITLE}}

Pull in the issue using `gh issue view <ID>`. If it has a parent PRD, pull that in too.

Only work on the issue specified.

Work on branch {{BRANCH}}. Make commits and run tests.

# CONTEXT

Here are the last 10 commits:

<recent-commits>

!`git log -n 10 --format="%H%n%ad%n%B---" --date=short`

</recent-commits>

# EXPLORATION

Explore the repo and fill your context window with relevant information that will allow you to complete the task.

Pay extra attention to test files that touch the relevant parts of the code.

# EXECUTION

If applicable, use RGR to complete the task.

1. RED: write one test
2. GREEN: write the implementation to pass that test
3. REPEAT until done
4. REFACTOR the code

# ENVIRONMENT

You are in a Linux (arm64) container with the full Qt 6.11 + Nim + Go toolchain. You CAN compile and run: `make tests-nim-linux` (Nim unit tests, `test/nim/`), status-go unit tests (`cd vendor/status-go && make test-unit`), and `make qml-lint`. You CANNOT run the desktop app, GUI/e2e tests, or macOS/iOS/Android builds — if the issue requires those, comment on the issue and stop.

A fresh worktree has UNINITIALIZED submodules (`vendor/` will be empty). Before building or testing, run:

```
git submodule update --init --recursive
```

First build in a fresh worktree is slow (submodule fetch + vendor deps) — that is expected; don't abort it.

# FEEDBACK LOOPS

Before committing, verify with the checks that match what you touched:

- Nim code: `make tests-nim-linux`, or a targeted `nim c -r test/nim/<file>.nim` while iterating
- status-go code: `cd vendor/status-go && make test-unit`
- QML: `make qml-lint`

Report actual results honestly — never claim a check you didn't run.

# COMMIT

Make a git commit. The commit message must:

1. Use this repo's style: `<type>(<scope>): summary` (e.g. `fix(wallet): ...`), types: feat/fix/perf/chore
2. Reference the issue (`#{{TASK_ID}}`)
3. Key decisions made
4. Files changed
5. Blockers or notes for next iteration

Keep it concise.

# THE ISSUE

If the task is not complete, leave a comment on the issue with what was done.

Do not close the issue - this will be done later.

Once complete, output <promise>COMPLETE</promise>.

# FINAL RULES

ONLY WORK ON A SINGLE TASK.
