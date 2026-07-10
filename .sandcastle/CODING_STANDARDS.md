# Coding Standards — status-desktop

Loaded by the reviewer agent. The authoritative references are `CLAUDE.md` (repo root) and `guidelines/QML_ARCHITECTURE_GUIDE.md`; this file is the review checklist distilled from them.

## Architecture

- Respect the layer flow: `QML UI ↔ View ↔ io_interface ↔ Module ↔ Controller → Services → status-go`. No layer-skipping (e.g. QML reaching into services).
- Nim UI modules follow the `view.nim` / `io_interface.nim` / `module.nim` / `controller.nim` structure — new code in an existing module must match it.
- StatusQ (`ui/StatusQ/`) is a generic component library: no app-domain logic or app imports inside it.

## Nim

- Procs `camelCase`; exported symbols marked with `*`; module-level constants `UPPER_SNAKE_CASE`.
- Types: `type ServiceName* = ref object of QObject`; use `method` only for virtual/overridable.
- Signal/slot registration and QObject lifetime: watch for leaks — every `newX()` QObject needs a clear owner or explicit `delete`.

## QML

- Follow `guidelines/QML_ARCHITECTURE_GUIDE.md`; changed files must pass `make qml-lint` (config `.qmllint.ini`).
- No business logic in QML — delegate to the view/module layer.
- Prefer StatusQ components over raw QtQuick controls where an equivalent exists.

## Testing

- Nim changes need a unit test in `test/nim/` when the changed logic is testable in isolation; run `make tests-nim-linux`.
- status-go changes (vendor/status-go): unit tests via `make test-unit` in that directory.
- The container cannot run the app or GUI tests — a change whose only verification is manual UI interaction must be flagged in the review, not approved silently.

## Review verdicts

- Reject commits that claim verification they didn't run (check the log honestly).
- Reject changes that touch generated files, vendor code, or unrelated modules without the issue calling for it.
