# Fleet Log

Smoke-test log for the Sandcastle containerized agent fleet
(see `docs/AGENT-WORKFLOW.md`, "Sandcastle fleet" section).

## 2026-07-10 — Autonomous merge validation (#5)

This entry was produced by the **Sandcastle loop** and landed **WITHOUT
human action**: the merger agent pushed the branch, opened the PR, waited
for CI to go green, and merged it — auto-closing the issue on merge. It
validates the autonomous merge phase (push → PR → CI → merge).

Branch: `sandcastle/issue-5`. Documentation-only; no code.

### Toolchain check (actual output from this container)

```
$ nim --version
Nim Compiler Version 2.2.11 [Linux: arm64]

$ go version
go version go1.24.7 linux/arm64
```

## 2026-07-10 — Toolchain versions from pinned image (#4)

Follow-up to #1. That run executed in the wrong container image (no
toolchain), as recorded below. The agent image is now pinned
(`status-desktop-agent:local`) and this entry validates the fix: the
Nim, Go, and Qt toolchains are present on `PATH` in this container.

### Toolchain versions (actual output from this container)

```
$ nim --version
Nim Compiler Version 2.2.11 [Linux: arm64]
Compiled at 2026-06-16
Copyright (c) 2006-2026 by Andreas Rumpf

git hash: 7b57dc1e54d2af08b12f995bb76cd44d663b3537
active boot switches: -d:release

$ go version
go version go1.24.7 linux/arm64

$ qmake --version
QMake version 3.1
Using Qt version 6.11.0 in /opt/qt/6.11.0/gcc_arm64/lib
```

All three toolchain binaries resolved on `PATH`, confirming the pinned
image mounts the Qt 6.11 + Nim + Go build environment. The Linux
verification loops (`make tests-nim-linux`, status-go unit tests,
`make qml-lint`) can now be exercised for future runs.

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
