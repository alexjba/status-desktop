# Fleet Log

Smoke-test log for the Sandcastle containerized agent fleet
(see `docs/AGENT-WORKFLOW.md`, "Sandcastle fleet" section).

## 2026-07-10 — Pipeline smoke test (#1)

This entry was produced by a **Sandcastle container agent** as an end-to-end
validation of the container agent loop (issue claim → branch → commit →
in-branch review → merge).

Branch: `sandcastle/issue-1`. No code changes — documentation only.

### Verification commands available in this container

The issue asked which verification commands were available, with their output.
Reported honestly from this run:

| Command | Result |
|---------|--------|
| `nim --version` | not available — `nim: command not found` |
| `go version` | not available — `go: command not found` |
| `qmake --version` | not available — `qmake: command not found` |

None of the three toolchain binaries were present on `PATH`, and a full
filesystem search found no `nim`, `go`, `qmake`, or `qmake6` binary; the
`vendor/` submodules were not initialized in this worktree. As a result the
Linux verification loops (`make tests-nim-linux`, status-go unit tests,
`make qml-lint`) could not be exercised for this run.

This does not affect the change itself, which is documentation-only and touches
no buildable code. The missing toolchain is noted here so the fleet operator can
confirm whether the CI build image (Qt 6.11 + Nim + Go) was mounted for this
container.
