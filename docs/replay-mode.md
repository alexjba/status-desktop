# Record / replay mode

Status: design accepted for implementation, in progress.

Record everything the Nim middleware hands to QML while the real app runs, then run the
same QML app from that recording with no Nim, no status-go and no Android service
process. Desktop and Android.

Uses: QML iteration and memory measurement without backend noise, repeatable, on
realistic account states (record on one machine, replay on another). First consumer:
the Android UI-process memory floor (status-im/status-app#21528).

Decision record: [ADR 0011](adr/0011-qml-record-replay-mode.md).

## Surface (master @ 4553a65ccd)

What Nim hands to QML, and how each kind is covered.

| Kind | Count | Where it enters QML | Covered by |
|---|---|---|---|
| Root context properties via nimqml | 47 names, 49 call sites in `src/` (`globalUtils`, `onboardingModule` set twice) | `QQmlApplicationEngine.setRootContextProperty` (nimqml-seaqt `qqmlapplicationengine.nim`) | context-property hook |
| Root context properties via C++ shim | 2 (`nimQtCameraBackend`, `nimQrScanBackend`) | `shimQmlInstall` in nim-qtcamera / nim-qrscan | root-context enumeration at each moment |
| Nim QObject types (`QtObject:`) | 225 | dynamic `QMetaObject` from `metaobjectgen.nim`, dispatch in `notherside.nim` `vtbl.metacall` | metaobject walk + metacall hook |
| List models (`QAbstractListModel`/`ItemModel`) | 80 | same, plus `rowCount`/`data`/`roleNames` vtable callbacks | `QAbstractItemModel` API from C++ |
| Properties | 895 (string 270, bool 243, QVariant 189, int 170, float 20, int64 3) | `ReadProperty` metacall | snapshot + notify-driven timeline |
| Signals | 823 (154 with arguments) | `QMetaObject::activate` | per-object generic connection |
| Slots | 1926 (string 382, bool 312, QVariant 195, int 192) | `InvokeMetaMethod` metacall | metacall hook: args + return value |
| Nested QObjects | QVariant properties, slot returns (submodules), role data, signal args | any `QVariant` holding `QObject*` | object registry, discovered on first sight |
| Lazy models | `collectibles_model` (`canFetchMore`/`fetchMore`) | model API | recorded as data; `fetchMore` calls as timeline events |
| Nim-set process state | app name/org/domain/version, desktop file name, `QSG_RENDER_LOOP`, `QSG_ATLAS_*`, HDPI/ui-scale, rcc registration, import paths, NAM factory + disk cache, boosted incubation controller, background graphics release, webengine init, `statusq_registerQmlTypes`, monitoring, message handler, extra CA cert | `src/nim_status_client.nim` startup | replay's own C++ setup + setup fingerprint |
| QML module import dirs added by Nim libs | NimQtCamera, NimQrScan (`qmlImportDir`, compile-time source path) | `shimQmlInstall` | engine `importPathList()` in the fingerprint |
| Main document | `qrc:///main.qml` | `app_controller.nim` | same URL |
| Images from status-go media server | every avatar, sticker, community image, chat image | `https://localhost:<port>/...` through the StatusQ NAM factory | media capture (phase 3b) |

Not present (checked): image providers registered from Nim (StatusQ only reads them),
`qmlRegisterType`/singletons from Nim, Nim-implemented QML types, Nim-created Qt C++
objects exposed to QML. Translations and fonts are loaded on the QML/StatusQ side, so
they are shared code already.

Nim → QML calls outside the object graph: `app_window.nim` raises the `mainWindow`
root object (not needed in replay); `signal_handler.nim` queues status-go signals into
the `signals` object (recorded as that object's signals).

## Architecture

```
record:  Nim QObjects ──metacall/context hooks──► Recorder (libStatusReplay, dlopen'ed) ──► recording dir
replay:  recording dir ──► Replayer (libStatusReplay) ──► Replay objects/models ──► same QML, matching engine setup
```

### Library placement

Recorder and replayer live in their own library, `ui/replay/` → `libStatusReplay`
(CMake, links Qt incl. Core/Qml private and StatusQ). It is not part of StatusQ and
normal builds do not link it:

- **Normal run**: Nim checks `STATUS_RECORD`. Unset → nothing is loaded, hooks stay
  nil, zero added pages. Set → Nim `dlopen`s `libStatusReplay` (next to the app
  binary, or `STATUS_REPLAY_LIB`), resolves `statusreplay_recorder_attach`, and
  installs the callbacks it returns into the nimqml-seaqt hooks. A missing library
  is logged and the app runs unrecorded.
- **Replay run**: the `status-replay` executable (desktop) and the Android replay
  variant link `libStatusReplay` directly.

### Choke points

Two hooks, both in nimqml-seaqt, each a nil-checked C function pointer, so the cost
when off is one branch per metacall. Developed on a local nimqml-seaqt branch pinned
in this work; the upstream seaqt PR and the final pin bump come later, on the owner's
go:

1. **Context property hook** in `setRootContextProperty`: `(engine, name, QVariant*)`.
   Needed for ordering: login sets `mainModule` and friends mid-run, and QML reacts to
   the moment they appear. No call-site edits in `src/`.
2. **Metacall hook** in `notherside.nim` `vtbl.metacall`, after dispatch:
   `(QObject*, call, index, argv)`. Records slot calls from QML with arguments and the
   return value, and `WriteProperty` from QML. Property reads are not hooked; values come
   from snapshots and notify signals.

Everything else is reached generically from C++ through `QMetaObject`/`QAbstractItemModel`
APIs, so new Nim types, properties and models need no recorder changes. The two
C++-shim context properties are caught by enumerating the root context at each moment
(`QQmlContextData::propertyNames()`, QmlPrivate, linked by `libStatusReplay` only).
Any root context property not seen through the hook is reported in the manifest as a
coverage gap.

### Recorder (`libStatusReplay`)

- Loaded only when `STATUS_RECORD=<dir>` is set (desktop env; Android: env from the
  launch intent). After `dlopen`, Nim calls `statusreplay_recorder_attach(engine)` once
  after creating the engine, and `statusreplay_recorder_markMoment(name)` (resolved
  the same way, skipped when not loaded) at two app-defined points.
- **Object registry**: every QObject reachable from context properties, QObject-valued
  properties, role data, slot returns and signal args gets a stable id in discovery
  order (deterministic traversal, so diffs between recordings stay small). On first
  sight: schema (if new), rows and roles for models, generic connections to all its
  signals, `destroyed` tracking.
- **Read-driven values**: the recorder never calls a property getter QML has not called.
  Property values come from the metacall hook when QML reads them (`val` events when a
  read returns something new); after that the recorder re-reads them on their notify
  signal and at each moment. Calling every getter eagerly crashed the first desktop
  recording (a Nim getter that is only valid once its module finished loading), and
  values QML never read are not needed to replay what QML showed.
- **Schemas** are keyed by a content hash, not class name: many Nim types share a name
  (`View`, `Model`). Schema = class name, base kind (`QObject`, `QAbstractListModel`,
  `QAbstractItemModel`), the full metaobject chain below the base (properties with type,
  flags, notify; signals and slots with parameter types and names; enums), and
  `roleNames()` for models.
- **Timeline** events, in emission order, with a monotonic sequence and a timestamp:
  `ctx` (context property set), `obj` (new object) + `init` (its rows), `del`, `val`
  (a property value QML read), `sig` (any signal: args, plus the new values of the
  properties it notifies), `call` (slot from QML: args, return), `write` (property
  written from QML), model ops `ins`/`rem`/`mov`/`data`/`reset`/`layout` (with the new
  row values for inserts, changed roles for `dataChanged`, full rows after reset/layout
  change), `load` (main document loaded), `moment`.
- **Moments**: `startup` (`STATUS_RECORD_STARTUP_DELAY_MS`, default 5 s after `main.qml`
  loads), `loggedIn` (Nim marks it when the main module finished loading) and
  `loggedIn-settled` (`STATUS_RECORD_SETTLE_MS`, default 60 s later), `exit`. At each
  moment the recorder writes a full snapshot of the live registry and the root context
  (so a replay can start at any moment without playing what came before) and a window
  screenshot (`screenshots/<moment>.png`) to compare with the replay.
- **Scripted recordings**: `STATUS_RECORD_TYPE` is typed into the focused QML item right
  after the startup moment (`\n` is Enter; e.g. a throwaway test profile's password),
  `STATUS_RECORD_QUIT_AFTER_MS` quits the app so the exit moment is written.
- **Privacy**: values of parameters and properties whose name matches
  `password|passphrase|mnemonic|seedphrase|privatekey|secret`, ends in `pin`/`puk`, or is
  `seed`/`pairingCode` are replaced by a marker, including keys inside JSON string
  payloads (the login request passes the password in a JSON argument); recordings go to
  `./Status/recordings/<timestamp>` (dev data dir, already gitignored) by default.
  Recordings from real accounts are never committed; host tests use a small synthetic
  fixture.

### Format (directory, versioned, diffable)

```
<recording>/
  manifest.json      formatVersion, app commit, Qt version, platform, DPR, startedAt,
                     setup fingerprint, moments [{name, seq, t, snapshot}], coverage gaps
  schemas/<hash>.json
  snapshots/<moment>.json   root context + every live object (props, model rows)
  timeline.jsonl     one event per line, {"seq","t","k",...}
  media/             phase 3b: captured media-server responses + index
```

Values are JSON, untagged when the declared type is concrete (string, bool, int,
int64, double) and tagged when it is `QVariant` or a container: `{"$obj":17}`,
`{"$t":"QUrl","v":"..."}`, `{"$list":[...]}`, `{"$map":{...}}`, `{"$t":"bytes","v":
"<base64>"}`, `null` for an invalid variant. Snapshots are written with sorted keys and
indentation; the timeline is compact JSONL. The replayer refuses an unknown major
`formatVersion`.

### Replayer (`libStatusReplay`)

- `ReplayObject` (QObject) and `ReplayModel` (QAbstractListModel /
  QAbstractItemModel) carry a dynamic `QMetaObject` built from the schema with
  `QMetaObjectBuilder` over the right base metaobject, with their own `qt_metacall`:
  - `ReadProperty` → stored value; `WriteProperty` → store, emit notify, log.
  - signals → `QMetaObject::activate` with recorded args.
  - slots → recorded return for the same object, slot and argument values, taking the
    latest one at or before the current timeline position; else the latest for the slot
    with any args; else the type's default. Every call is logged
    (`<recording>/replay-logs/<time>-calls.jsonl`) with whether it matched, which is the
    main gap signal.
  - models serve `rowCount`/`data`/`roleNames` from stored rows; `canFetchMore` follows
    the recording.
- **Player**: builds the object graph from a snapshot (`--moment <name>`; without it,
  from the beginning, applying everything up to the `load` event at once) and sets the
  root context properties before loading `main.qml`, then plays the timeline from that
  point in one of three modes: `realtime` (recorded timestamps, `--speed`; F10
  pauses), `step` (F8 next event, F9 next moment), `frozen` (snapshot only).
  `--quit-after <ms>` and `--screenshot <png>` make runs scriptable. Model ops map 1:1 to `begin*/end*` calls, so QML sees the
  same insert/remove/move/reset sequence as in the recording.
- **Entry point**: `statusreplay_main(argc, argv)` builds the app with the replay setup
  and runs the player. Desktop: a small `status-replay` executable linking
  `libStatusReplay` and loading the app's `resources.rcc`. Android: the wrapper app's
  `main.cpp` calls it instead of `NimMain()` in the replay variant.

### Setup parity

The Nim startup in `src/nim_status_client.nim` stays as it is. The replayer has its own
C++ setup that mirrors the Qt-side part (app identity, env defaults, HDPI, rcc
registration, import paths, NAM factory, incubation controller, background graphics
release, webengine init, StatusQ QML type registration, monitoring) and leaves out what
is Nim-only (logging callback, status-go cert, single instance, URL manager, camera/
QR-scan backends).

Drift between the two is caught by a setup fingerprint: app identity, relevant env
vars, engine import paths, root context property names, rcc roots, incubation
settings, DPR, graphics API, QML types registered under the app's URIs. The recorder
computes it from the live app and stores it in the manifest; the replayer computes the
same fingerprint after its setup, diffs it at startup and prints every mismatch; a host
test asserts zero mismatches against the fingerprint recorded in the fixture.

Nim-library QML modules (NimQtCamera, NimQrScan) load from the recorded import paths;
their backends replay as ordinary recorded objects. No camera frames.

## Make targets

| Target | What it does |
|---|---|
| `make record-run [RECORDING=<dir>]` | builds `libStatusReplay` next to the app, `make run` with `STATUS_RECORD` set; prints the recording path on exit |
| `make replay-run RECORDING=<dir> [REPLAY_MOMENT=<name>] [REPLAY_MODE=realtime\|step\|frozen]` | builds StatusQ + `libStatusReplay` + `status-replay` + `resources.rcc` (no Nim, no status-go) and runs it |
| `make mobile-record-run [RECORDING=<dir>]` | `mobile-run` with `libStatusReplay` added to the APK (not linked) and recording on; pulls the recording on exit |
| `make mobile-replay-run RECORDING=<dir> [...]` | builds the Android replay variant (`BUILD_VARIANT=replay`: no `libnim_status_client`, `libstatus_stub` or status-go service), pushes the recording into the app's files dir (`run-as`), launches |
| `make run-replay-tests` | host round-trip and parity tests for `libStatusReplay` |

The Android replay variant has its own package id (`app.status.mobile.replay`,
overridable), so it installs next to the real app.

## Tests (host, ctest under `ui/replay/tests`)

- Round trip: a fake C++ QObject tree (nested objects, QVariant-held objects, a list
  model with object-valued roles, slots with and without args, notify and plain
  signals) → record → replay → same metaobject signatures, property values, model rows,
  and the same order of notifications seen by a QML consumer.
- Timeline: scripted mutations (property changes, inserts/removes/moves/resets, object
  creation and deletion, context property added mid-run) → recorded order equals
  replayed order observed through `QSignalSpy`.
- Slot matching: exact args, fallback to latest, typed default; redaction of sensitive
  parameters.
- Format: committed synthetic fixture loads; unknown major version is rejected.
- Setup parity: the replay setup's fingerprint matches the one recorded from the real
  app.
- Loading: a normal run without `STATUS_RECORD` maps no `libStatusReplay` pages.
- nimqml-seaqt side: a Nim test that both hooks fire with the right arguments.

## Phases

0. This doc + ADR.
1. nimqml-seaqt hooks (local branch), `libStatusReplay` recorder + static-snapshot
   replay (login screen, wallet), replay setup + fingerprint, make targets, host tests.
2. Timeline replay: pre-login → login → post-login walks by itself.
3. Android replay variant + `mobile-replay-run`; Lenovo smoke, then Redmi.
3b. Media capture through the NAM factory; replay serves captured responses.
4. Validation against real runs on the same moments: Mac first (loaded/instantiated
   composite types, heap), then Redmi (TOTAL PSS + SwapPss, native heap).

## Out of scope

- Replaying user input; QML-internal state (current page, scroll position, popups)
  only matches where QML derives it from Nim state.
- Driving the backend from replay: slot calls never change replayed state, they only
  return recorded values.
- Web content (browser section, dApps WebEngine pages), camera frames, keycard, iOS.
- Editing or synthesizing recordings beyond the test fixture.
- Exact async timing (status-go response latency, incubation interleaving) beyond the
  recorded timestamps.

## Risks

- **Slot coverage**: a slot that QML calls in replay but never called during
  recording (or with other args) returns a default; views gated on such a call may
  render empty. Mitigation: the call log shows every miss; recording longer walks
  fills it.
- **Interaction drift in timeline replay**: the recording reflects what the recording
  user clicked; replayed QML does not click, so Nim state can point at a section the
  replayed UI is not showing. Moments + snapshot starts keep static measurements exact.
- **Cross-platform recordings**: Nim takes `android`/`ios` branches (feature flags,
  paths), so a desktop recording replayed on Android is not the Android app's state.
  For memory numbers, record on the device (`mobile-record-run`).
- **Size**: big models (messages, collectibles, tokens) make snapshots tens of MB;
  per-moment snapshots multiply that. Acceptable for a dev tool; compression later if
  needed.
- **Recording overhead** changes timing and memory of the recorded run; record runs
  are not used for measurements, replay runs and plain runs are.
- **Upstream dependency**: the hooks live on a local nimqml-seaqt branch until the
  seaqt PR merges; the submodule pin points at that branch meanwhile.
- **Setup drift**: the replay setup duplicates Nim's Qt-side startup; a change on one
  side only shows up as a fingerprint mismatch, so the parity test must run in CI with
  the other replay tests.
- **Private Qt API** (`QMetaObjectBuilder`, `QQmlContextData`) can change between Qt
  minors; both are confined to `libStatusReplay` and covered by the host tests.
- **Media**: before phase 3b every status-go-served image is missing in replay, which
  lowers image memory and texture use versus a real run; numbers taken before 3b must
  state it.
