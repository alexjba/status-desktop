# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Technology Stack

| Layer | Technology |
|-------|-----------|
| Frontend | QML/Qt 6 (declarative UI) |
| UI Components | StatusQ — in-repo component library (`ui/StatusQ/`) |
| Middleware | Nim 2.2.x — bridges QML ↔ Go via NimQML/DOtherside |
| Backend | Go (`vendor/status-go/` submodule) |
| Networking | Waku (P2P messaging) |
| Build | GNU Make + CMake/qmake |

## Build & Run

Requires Qt 6 installed with `qmake` on `PATH`. See `BUILDING.md` for full setup per platform.

```bash
make update          # Initialize submodules and build dependencies (first time)
make run             # Build and run the app
make pkg             # Create distribution package for current platform
make clean           # Clean build artifacts
```

Build options via environment variables:
- `QML_DEBUG=true` — enable QML debugger on port 49152
- `MONITORING=true` — enable QML monitoring tools
- `INCLUDE_DEBUG_SYMBOLS=1` — include debug symbols

Developer builds store user data in `./Status/` at the repo root (not the OS user data directory).

For VS Code Nim support: `./env.sh code .`

## Testing

```bash
make tests-nim-linux          # Run all Nim unit tests (Linux only)
make run-statusq-tests        # Run StatusQ/Qt unit tests via ctest
make run-storybook-tests      # Run storybook page validator tests
make run-storybook            # Launch interactive storybook UI
```

Nim unit tests live in `test/nim/`. UI/BDD tests (Python + Gherkin) are in `test/ui-test/`.

To run a single Nim test file, pass it via `nim c -r`:
```bash
nim c -r test/nim/activity_tests.nim
```

## Linting

```bash
make qml-lint          # Lint QML files (config: .qmllint.ini)
make qml-lint-mobile   # Lint mobile QML files
```

C++ code uses `.clang-format` and `.clang-tidy` for formatting and static analysis.

## Architecture

The data flow is:

```
Frontend (QML) → StatusQ components
              → DOtherside → NimQML
                             → Nim Middleware (src/)
                               → nim-status-go → status-go (vendor/status-go/)
                                                  → Waku (P2P), Wallet providers, Local DBs
```

### Nim Middleware (`src/`)

Standard module pattern: `UI ↔ View ↔ Interface ↔ Module ↔ Controller → Services → status-go`

Key directories:
- `src/app/modules/main/` — top-level app sections (Chat, Wallet, Communities, Profile, etc.)
- `src/app/modules/shared_models/` — shared data models
- `src/backend/` — service connectors to status-go (wallet, chat, communities, ENS, activity, settings)
- `src/constants.nim` — global constants

### QML Frontend (`ui/`)

- `ui/StatusQ/` — reusable component library with design system
- `ui/app/` — application-level QML components
- `ui/imports/` — QML module imports
- `ui/i18n/` — internationalization files

### QML Architecture Rules (from `guidelines/QML_ARCHITECTURE_GUIDE.md`)

**Core principles:**
- **Singletons must be stateless** — no mutable state, no backend references in singletons
- **Components expose dependencies explicitly** — public API should reveal all requirements; avoid hidden access via singletons
- **Favor composition over parameterization** — prefer composing components over deeply parameterized ones
- **Only the store layer accesses the backend** — components receive data through their API, not via singletons

**Stores:**
- Thin wrappers only — no data transformations (those belong in adaptors)
- Must completely hide backend context properties behind a clean wrapper
- Exposed properties must be read-only; state changes via methods only
- Must be explicitly typed: `required property TypedStore store`, not `property var store`
- A single backend context property should only be exposed once across all stores
- Low-level components emit intent-based signals; only high-level components access stores directly

**Adaptors:**
- Data-oriented, not view-oriented — name by transformation (e.g., `GroupingModel`), not by component
- Take plain models with well-specified roles, not entire stores
- Can be chained (output of one feeds into another)

**Popups/Modals:**
- Never instantiate popups directly inside components
- Use `popupRequestsHandler` pattern (e.g., `popupRequestsHandler.swapModalHandler.launchSwap()`)
- `HandlersManager` for centralized popup handling

**Code style:**
- Top-level component `id` must be `root`
- Private properties go in `QtObject { id: d }` block
- Do not set `objectName` on top-level components (parent sets it)
- `qmldir` entries must be sorted
- Use curly brackets for complex expressions (no ternary chains)
- Favor declarative implementation over imperative code in QML
- Avoid QML dynamic scoping (see [Qt docs on component scope](https://doc.qt.io/qt-6/qtqml-documents-scope.html))
- Follow [Qt QML Coding Conventions](https://doc.qt.io/qt-6/qml-codingconventions.html)
- C++ code follows [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)

**Models:**
- Always use `key` as the unique identifier role name
- Never use `model.rowCount()` in bindings — use `model.ModelCount.count`
- Never bind to whole row objects (deleted on model reset)
- Action signals should pass `index` or `key`, not metadata; receiver fetches data

**Components:**
- Avoid `anchors.fill: parent` in reusable components (breaks in Layouts); prefer `Control` as base
- Financial balances must be big integer strings; convert to display form only at UI layer

**Storybook & Testing:**
- Use identified module imports (`import X.Y 1.0`) — enables stub mechanism; relative imports (`import "./stores"`) break stubs
- Storybook pages must be functional in isolation — if hard to instantiate, the component API needs work
- Page classification: Good (full API interaction, no workarounds, no errors), Decent (most functionality, minor warnings), Bad (missing elements, many errors)
- Tests must be independent and isolated; mock dependencies, provide simple `ListModel` input data
- Tests should test the unit, not its subcomponents — verify subcomponents are used correctly, assume they work
- Avoid `wait(...)` — flakey; use synchronous patterns
- Tests must not generate warnings/debug logs (use `ignoreWarning` for expected ones)
- Single storybook page = single component + minimal test data and auxiliary controls

### Storybook (`storybook/`)

Interactive component development environment. Pages are in `storybook/pages/`. Used for isolated QML component development and testing.

## Internationalization

```bash
make update-translations    # Generate/update base TS files in ui/i18n/
make compile-translations   # Compile TS → QM binary files
```

Base files: `qml_base.ts` (strings) and `qml_en.ts` (plural forms). Lokalise auto-pulls from master for translators. See `I18N.md` for full guide.

## Nim Conventions

No formal style guide exists. Observed patterns:

- **Types:** `type ServiceName* = ref object of QObject` (`*` = exported)
- **Procs:** `camelCase` — `proc getName*(self: ServiceType): string` (exported), `proc helper(self: ServiceType)` (private)
- **Methods:** `method name*(self: Type)` for virtual/overridable
- **Constants:** `UPPER_SNAKE_CASE` for module-level
- **Module structure:** Each module follows `view.nim` / `io_interface.nim` / `module.nim` / `controller.nim`
- **Standard pattern:** `UI ↔ View ↔ Interface ↔ Module ↔ Controller → Services → status-go`

## Key Docs

- `BUILDING.md` — full platform build instructions
- `docs/architecture.md` — architecture diagrams (Mermaid)
- `guidelines/QML_ARCHITECTURE_GUIDE.md` — QML best practices (full reference)
- `docs/adr/` — Architecture Decision Records
- `CONTEXT.md` — domain glossary (canonical terms)
- `docs/AGENT-WORKFLOW.md` — agent fleet coordination rules (fork only)
- `I18N.md` — internationalization guide

## Branch Policy

This doc is shared between two checkouts. Check `git remote get-url origin` to know which rules apply:

- **`alexjba/status-desktop` (personal fork, agent fleet):** all PRs are fork-internal and target the fork's `master`. NEVER open a PR against `status-im/status-desktop` — upstreaming is done manually by the human. `master` is synced with upstream manually; check base freshness before starting work (see `docs/AGENT-WORKFLOW.md`).
- **`status-im/status-desktop` (upstream clone):** PRs must target `develop` (for features/fixes) or a release branch. The `master` branch is protected. CI (`pr.yml`) enforces status-go submodule branch policies and commit recency.

---

## status-go (`vendor/status-go/`)

The Go backend lives in `vendor/status-go/` as a submodule (`github.com/status-im/status-go`). Work done there is built and tested independently from the Desktop repo.

### Build & Test (run from `vendor/status-go/`)

```bash
make statusgo                    # Build the HTTP server (build/bin/status-backend)
make run-status-backend          # Start HTTP server (env: PORT)
make generate                    # Run code generation (incremental via go-generate-fast)
make test-unit                   # All unit + integration tests
make test-single PKG=./server/... TEST=TestFoo   # Single test (-testify.m match, not -run)
make test-unit-race              # With -race flag
make lint                        # generate + lint-panics + golangci-lint
make lint-fix                    # same with --fix
make statusgo-library            # Build static library
make statusgo-shared-library     # Build shared library (.dylib/.so/.dll)
make build-libsds                # Build Nim SDS dependency (needed if CGO errors mention libsds.h)
```

### Coding Standards

- Code follows [Effective Go](https://golang.org/doc/effective_go.html) and [CodeReviewComments](https://github.com/golang/go/wiki/CodeReviewComments)
- Commit format: [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
- PRs require 2 core dev reviews; use "Squash and merge" for clean history
- 50% minimum patch coverage (CI enforced)
- Every `go func()` must start with `defer common.LogOnPanic()` — CI hard failure

### Linting

1. **golangci-lint** — enabled: `errcheck`, `gosec`, `govet`, `ineffassign`, `misspell`, `unconvert`; formatter: `goimports` (local prefix `github.com/status-im/status-go`). Excludes: `internal/contracts/`.
2. **lint-panics** (`goroutine-defer-guard`) — every goroutine must defer `common.LogOnPanic()`.

### Architecture layers

```
mobile/status.go        Public API (100+ JSON-in/JSON-out functions); HTTP: POST /statusgo/<FuncName>
services/*/api.go       JSON-RPC namespaces (wallet, chat, ens, …)
protocol/messenger.go   Core messaging / community protocol (Protobuf wire format)
pkg/backend/            Node lifecycle, service registry (wraps go-ethereum)
internal/db/            SQLite via go-sqlcipher; migrations in migrations/sql/
```

### Key packages

| Package | Role |
|---|---|
| `protocol/` | `Messenger` manages chats, contacts, communities, installations. |
| `protocol/requests/` | Typed request structs (90+) each with `Validate()`. |
| `services/` | One sub-package per domain. Each has `service.go` + `api.go` (JSON-RPC namespace). |
| `server/` | Generic HTTP/TLS server with `Start`/`Stop`/`ToBackground`/`ToForeground` lifecycle. |
| `signal/` | Event bus — backend emits signals, Desktop subscribes via WebSocket `/signals`. |
| `params/` | `NodeConfig` and all configuration defaults. |
| `common/` | `LogOnPanic()` — must be deferred in every goroutine. |

### Mobile/Public API Pattern

All exported functions in `mobile/status.go` follow the same shape:

```go
func FuncName(requestJSON string) string {
    // 1. json.Unmarshal → typed request struct (from protocol/requests/)
    // 2. request.Validate() → return error JSON if invalid
    // 3. Call business logic
    // 4. Return makeJSONResponse(result) or makeJSONResponse(err)
}
```

Called via HTTP: `POST /statusgo/<FuncName>`. To add a new API function:
1. Create a request struct in `protocol/requests/` with `Validate()` method
2. Add the exported function in `mobile/status.go`
3. Implement business logic in the appropriate service or protocol layer

### Service Structure

Each service in `services/` follows a standard pattern:

- **`service.go`** — `Service` struct with `NewService()`, `Start()`, `Stop()`, `APIs()`
  - `APIs()` returns `[]gethrpc.API` with namespace, version, and service pointer
- **`api.go`** — `API` struct wrapping `Service`; methods registered as JSON-RPC endpoints (e.g., `wallet.GetBalances`)
- Services are registered in `pkg/backend/` service registry

### Signal Events

Signals are how status-go communicates events to the frontend (Desktop subscribes via WebSocket `/signals`).

- Events defined as constants in `signal/events_*.go` (e.g., `events_messenger.go`, `events_wallet.go`)
- Event structs have JSON tags; wrapped in `Envelope{Type, Event, Timestamp}`
- Emitted via helper functions: `signal.SendMessageDelivered(chatID, messageID)`
- Pattern: define event constant + struct + `Send*()` helper in the relevant `events_*.go` file

### Database Migrations

- **UP-only** — no DOWN migrations (too expensive in SQLite)
- Filenames must be **Unix timestamps** + description: `1763580000_add_column.up.sql`
- CI enforces numeric ordering
- **Avoid writes to large tables** (e.g., `user_messages`) — causes slow upgrades
- Add as the highest-numbered file in the relevant `migrations/sql/` directory

### Testing Patterns

- Use **testify** suites, not raw `*testing.T`
- Call `dbsetup.ReducedKDFIterationsNumber()` in `TestMain` to speed up SQLCipher KDF
- Use `t.TempDir()` for temp files; HTTP tests use `httptest.Server`
- `-testify.m` flag for single test matching (not `-run`)

### Important patterns

- **`server.Server` contains a `sync.Mutex`** — never embed or copy by value. Always use `*Server`.
- **Code generation** — generated files are checked in; run `make generate` after editing `//go:generate` files (mocks, protobuf, bindata). Uses `go-generate-fast` for incremental builds.

---

## Mobile Development

The mobile app shares the same QML frontend and Nim middleware as desktop, with a platform-specific build pipeline.

### Mobile Build Quick Reference

**Critical:** Always prepend Nim to PATH and use `USE_SYSTEM_NIM=1` to avoid nim-sds issues.

```bash
export PATH="$REPO_ROOT/vendor/nimbus-build-system/vendor/Nim/bin:$PATH"
export QMAKE=<path-to-qt>/android_arm64_v8a/bin/qmake  # or ios/bin/qmake
make mobile-run -j10 V=3 USE_SYSTEM_NIM=1
```

| Command | Description |
|---------|-------------|
| `make mobile-run` | Build + deploy + run + logcat |
| `make mobile-build` | Build only (APK/app) |
| `make mobile-clean` | Clean all mobile build artifacts |
| `make qml-lint-mobile` | Lint QML against mobile Qt modules |

### Mobile Environment Variables

**Android** (all required):
- `QMAKE` — Qt 6.9.2 android_arm64_v8a qmake path
- `ANDROID_SDK_ROOT` — Android SDK path
- `ANDROID_NDK_ROOT` — NDK 27.2.12479018
- `JAVA_HOME` — JDK 17

**iOS**:
- `QMAKE` — Qt 6.9.2 ios qmake path
- `IPHONE_SDK` — `iphonesimulator` (default) or `iphoneos` (device)
- `QMAKE_DEVELOPMENT_TEAM` — Apple team ID for code signing

See `mobile/DEV_SETUP.md` for full environment setup per platform.

### Mobile Architecture

**Android two-process model** (see `docs/adr/0001-android-status-go-as-a-service.md`):
- **UI process** — QML/Qt app, links a stub `libstatus` that forwards via Binder IPC
- **Service process** — runs real status-go, survives UI process death
- IPC via AIDL with `call(method, argsJson)` shape
- Signals: service emits JSON to UI via `RemoteCallbackList`

**iOS** — single-process, status-go runs in-process.

### Mobile Debugging

```bash
# Android: get UI process logs
adb logcat --pid=$(adb shell pidof app.status.mobile) -v time -d | tail -100

# Android: service process (status-go) logs
adb logcat -d | grep -E "(StatusGoService|status-go|GoLog)" | tail -100

# iOS simulator logs
xcrun simctl spawn booted log stream --predicate 'subsystem == "app.status.mobile"'
```

Common issues: see `mobile/TROUBLESHOOTING.md`

### Mobile Directory Structure

```
mobile/
  bin/           — Final outputs (APK, .app)
  lib/           — Compiled native libraries
  build/         — Intermediate build files
  scripts/       — Build scripts, env setup, platform helpers
  android/qt6/   — Android sources (Java, AIDL, Gradle, manifests)
  ios/           — iOS config (entitlements, Info.plist, assets)
  wrapperApp/    — Qt mobile app wrapper project
  statusgo_stub/ — C API stub for Android UI process
```

### Key Mobile Docs

- `mobile/DEV_SETUP.md` — Developer environment setup for iOS and Android
- `mobile/README.md` — Quick start (container builds)
- `mobile/TROUBLESHOOTING.md` — Common build/runtime issues
- `docs/adr/0001-android-status-go-as-a-service.md` — Android two-process architecture
- `docs/adr/0002-android-push-notifications.md` — Waku-driven local notifications
- `docs/adr/0004-ios-push-notifications-decryption.md` — iOS push with NSE decryption
