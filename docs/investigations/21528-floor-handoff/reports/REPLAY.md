# impl-replay: record Nim→QML state, replay the QML app without Nim (permanent tooling)

Owner decisions (fixed): desktop + Android; snapshots + signal timeline; lands on upstream `master` (permanent, PR-able). Make targets `make record-run` (records all Nim states, pre-login and after login) and `make replay-run` (runs the app from a recording).

## Why
QML iteration and memory measurement without status-go/Nim noise, repeatable, and on realistic account states (record on desktop, replay on the Redmi). First consumer: issue status-im/status-app#21528 (Android UI-process memory floor). Read spikes-floor/PATH-TO-150.md and qmlmeta-report.md in /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/ for context only.

## Surface to cover (measured on spike/floor-final)
49 context properties, 225 Nim QObject types, 82 list models, 895 properties, 1926 slots. Plus anything else Nim hands to QML: singletons/types registered from Nim, image providers, URL schemes, nested QObject properties, submodels returned by slots, QVariant payloads. Inventory it all first (src/, vendor/nimqml-seaqt) and list gaps.

## Workspace
- `git -C ~/Repos/status-desktop fetch origin && git -C ~/Repos/status-desktop worktree add ~/Repos/status-desktop.worktrees/replay-mode -b feat/replay-mode origin/master`. Upstream clone, NOT the alexjba fork. Never touch ~/Repos/status-desktop's working tree (use `git -C`), no bare `git stash`.
- Fresh-worktree build gotchas: /Users/alexjbanca/.claude/projects/-Users-alexjbanca-Repos-status-desktop/memory/reference_fresh_worktree_mobile_build_gotchas.md, reference_desktop_build_gotchas.md, feedback_mobile_build_flags.md, feedback_mobile_build_variants.md, feedback_concurrent_builds.md.
- Desktop build: see the `desktop-run` skill (QMAKE=~/Qt/6.11.0/macos/bin/qmake, Nim on PATH, USE_SYSTEM_NIM=1). Android: `mobile-build` skill; your own BUNDLE_IDENTIFIER app.status.freplay.
- Every build (desktop or mobile) through the build lease: `LEASE_WAIT_MAX=7200 bash ~/Repos/status-desktop.worktrees/perf-21528-memory/mobile/scripts/profiling/lease.sh run buildbox impl-replay 21528 90 "<purpose>" -- bash <script>; echo $? > <job>.done`, then wait with a Monitor until-loop on the .done (re-arm on each 30-min expiry). Never rely only on a background notice.
- Devices: Lenovo first (`lease.sh run lenovo ...`), Redmi only for final numbers and ≤25 min per job, whole job scripted inside the lease. Device rules, adb serial, profile setup: spikes-floor/COMMON.md. Only your own package; never touch app.status.mobile, app.status.mobile.debug, app.status.mobile.pr. Don't kill the adb server.

## Architecture (starting points; the design doc decides, owner reviews)
- **Recorder:** compiled into normal builds, off unless enabled (env/flag). A single choke point for every Nim→QML handoff (wrap where nimqml/seaqt sets context properties and registers types, rather than 49 call sites). Walks QMetaObject: properties (values, nested QObjects), signals, invokables; models: roleNames + rows; subscribes to notify signals and model change signals (insert/remove/move/dataChanged/reset) for the timeline; records slot calls from QML with args and return values. Moments: pre-login (startup/onboarding/login screen), after login, then a continuous timeline.
- **Format:** versioned, diffable directory: manifest (app commit, Qt version, platform, moments), schemas (one per metaobject), snapshots per moment, timeline (JSONL, timestamped). Recordings live outside the repo by default (gitignored path); they contain account data, so nothing recorded from a real account is committed. A tiny synthetic fixture for tests is fine.
- **Replayer:** a C++ library plus a replay entry point with NO libnim/status-go/service process. Builds objects from schemas (QMetaObjectBuilder or an equivalent), a generic list model, serves snapshot values, plays the timeline (real-time and step modes), slots return the recorded value for matching args or a typed default and are logged. App setup that Nim does today (QGuiApplication, StatusQ registration, import paths, fonts, translations, image providers, main.qml) must match; prefer sharing one setup path over duplicating it, and test the parity.
- **Build/make:** `make record-run` (desktop app with the recorder on), `make replay-run RECORDING=<dir>` (desktop), plus mobile equivalents (e.g. `make mobile-replay-run`, an Android variant without libnim/libstatus/service). Names and options are yours to propose in the design doc.
- Follow CLAUDE.md, guidelines/QML_ARCHITECTURE_GUIDE.md (QML must not change for replay), C++ Core Guidelines, existing repo idioms. Use the `tdd` skill: host tests (round-trip: fake QObject tree → record → replay → same metaobject, values, model rows, timeline order).

## Phases and gates
0. **Design doc + inventory (no implementation code).** docs/replay-mode.md (architecture, format, make targets, what's out of scope, risks) + an ADR draft in docs/adr/ (next free number on master). Send it to the lead (SendMessage to the team lead) and WAIT for go. Design choices later in the work also go to the lead before committing.
1. Desktop recorder + static-snapshot replay (login screen, wallet) + make targets + host tests.
2. Timeline replay: pre-login → login → post-login walks by itself.
3. Android replay variant + mobile make target; Lenovo smoke, then Redmi.
4. Validation for the memory work: real run vs replay on the same moments, Mac first (type-closure probe ~/Repos/research-qmlmeta, see memory reference_qml_type_closure_probe.md: loaded/instantiated composite types, heap) then Redmi (TOTAL PSS+SwapPss, native). Report how close replay is and what differs.

## Rules
- Signed commits, one logical step each, NO Co-Authored-By trailer. NO push, NO PR, NO GitHub comments until the owner says so.
- If you spawn sub-agents: `run_in_background: true` (boolean).
- Report: spikes-floor/replay-report.md, marked WIP until done; per phase what works, test results, open gaps. Messages to the lead ≤10 lines.
- Blocked (permission denied, build broken >30 min) → stop and report; never work around a denial or ask another agent to do it.
