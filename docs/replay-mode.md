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
| Images from status-go media server | every avatar, sticker, community image, chat image | `https://localhost:<port>/...` through the StatusQ NAM factory | media capture: StatusQ reply tap / request interceptor |

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
  nil, zero added pages. Production builds compile the check out entirely (it is on by
  default in non-production builds; `-d:statusRecorder` re-enables it). Set → Nim `dlopen`s `libStatusReplay` (next to the app
  binary, or `STATUS_REPLAY_LIB`), resolves `statusreplay_recorder_attach`, and
  installs the callbacks it returns into the nimqml-seaqt hooks. A missing library
  is logged and the app runs unrecorded.
- **Replay run**: the `status-replay` executable (desktop) and the Android replay
  variant link `libStatusReplay` directly.

### Choke points

Three hooks, all in nimqml-seaqt, each a nil-checked C function pointer, so the cost
when off is one branch per metacall or data() call. They are developed on a local
nimqml-seaqt branch (`replay-hooks`) that the submodule pin points at. **That commit
exists only locally: the pin is unreachable for anyone else until the seaqt PR is
merged and the pin is bumped (owner's go). This blocks merging this work upstream.**
Earlier pins of this branch stay reachable locally as `replay-hooks-v0` (6c78457) and
`replay-hooks-v1` (f2caa4f), so older commits of this work still check out.

1. **Context property hook** in `setRootContextProperty`: `(engine, name, QVariant*)`.
   Needed for ordering: login sets `mainModule` and friends mid-run, and QML reacts to
   the moment they appear. No call-site edits in `src/`.
2. **Metacall hook** in `notherside.nim` `vtbl.metacall`, after dispatch:
   `(QObject*, call, index, argv)`. Records slot calls from QML with arguments and the
   return value, `WriteProperty` from QML, and `ReadProperty` (what QML read, see below).
3. **Model data hook** in the model `vtbl.data`, after Nim answered: `(model, row, role,
   QVariant*)`. Tells which roles QML reads.

Everything else is reached generically from C++ through `QMetaObject`/`QAbstractItemModel`
APIs, so new Nim types, properties and models need no recorder changes. The two
C++-shim context properties are caught by enumerating the root context at each moment
(`QQmlContextData::propertyNames()`, QmlPrivate, linked by `libStatusReplay` only).
Any root context property not seen through the hook is reported in the manifest as a
coverage gap.

### Recorder (`libStatusReplay`)

- Loaded only when `STATUS_RECORD=<dir>` is set (desktop env; Android: the
  `status-env.txt` file debug packages read at start). Without it, recordings go to
  `<app data dir>/recordings/<timestamp>`. After `dlopen`, Nim calls `statusreplay_recorder_attach(engine)` once
  after creating the engine, and `statusreplay_recorder_markMoment(name)` (resolved
  the same way, skipped when not loaded) at two app-defined points.
- **Object registry**: every QObject Nim hands out (context properties, QObject-valued
  properties, role data, slot returns, signal args) gets a stable id in discovery order.
  On first sight: schema (if new), role names and row count for models, generic
  connections to all its signals, `destroyed` tracking. Objects QML passes in (slot
  arguments, property writes) that Nim never handed out are referenced by class only and
  not followed. Metacalls on objects that are not registered (or were deleted by the call
  itself) are ignored.
- **Read-driven values**: the recorder never calls a property getter QML has not called.
  Property values come from the metacall hook when QML reads them (`val` events when a
  read returns something new); after that the recorder re-reads them on their notify
  signal and at each moment. Calling every getter eagerly crashed the first desktop
  recording (a Nim getter that is only valid once its module finished loading), and
  values QML never read are not needed to replay what QML showed.
- **Read-driven roles**, the same for models: a role is read back only once QML read
  it (model data hook). Its first read writes the whole column (`col` event); later
  inserts, `dataChanged`, resets and snapshots carry the known roles only.
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
  (so a replay can start at any moment without playing what came before). Scripted runs
  also save a window screenshot (`screenshots/<moment>.png`) to compare with the replay;
  hand-driven ones only with `STATUS_RECORD_SCREENSHOTS=1`.
- **Unattended recordings**: `STATUS_RECORD_SCRIPT` names a JSON script
  (`ui/replay/scripts/`) that the recorder runs after the startup moment. Steps find a
  visible, enabled item by `objectName` and click it, type into it, set a switch, wait
  for it, sleep or mark a moment (`ScriptRunner`); a failing step lists the named items
  that were visible. Clicks and switches are also written to the timeline (`ui`
  events) so replay performs them again; typed text is not recorded.
  `STATUS_RECORD_QUIT_AFTER_SCRIPT_MS` quits after the script, `STATUS_RECORD_QUIT_AFTER_MS`
  after a fixed time. With `STATUS_RECORD_SCRIPT` alone (no `STATUS_RECORD`) the script
  drives the app and nothing is observed or written: that is how the real app is measured
  at a recording's moments (`STATUS_RECORD_QUIT_AFTER_MS` still ends the run). Scripts use the throwaway test password only; all of this
  lives in libStatusReplay, which release builds never load unless built with
  `RECORDER=1` (mobile: packages the library and compiles Nim with `-d:statusRecorder`).
- **Privacy**: values of parameters and properties whose name matches
  `password|passphrase|mnemonic|seedphrase|privatekey|secret|credential|connectionstring`,
  ends in `pin`/`puk`, or is
  `seed`/`pairingCode` are replaced by a marker whatever their type (text by the marker
  string, other values by a marker that replays as the type's default), including keys
  inside JSON string payloads (the login request passes the password in a JSON argument).
  `make record-run` writes to `Status/recordings/<timestamp>` (dev data dir, gitignored).
  Recordings from real accounts are never committed; host tests use a small synthetic
  fixture.

### `make record-run` (unattended)

`make record-run` needs no manual step on a clean machine:

1. It uses its own data dir (`RECORD_DATADIR`, default `Status/record-profile`), so the
   developer's profiles are never touched.
2. Without a profile there, it runs the app with `create-profile.json`: onboarding,
   "Create profile" with password `TestPassword2026`, "Save password" off (when the
   platform offers it), until the main app shows. That run is itself recorded
   (`<datadir>/onboarding-recording`).
3. It then records a run that starts at the login screen (`startup`), logs in
   (`login.json`), waits for the main app (`loggedIn`), opens the wallet and marks
   `wallet` 30 s later, then quits (`exit`).
4. It fails unless the recording has the `startup`, `loggedIn` and `exit` moments.

`RECORDING=<dir>` picks the output (default `Status/recordings/<timestamp>`);
`RECORD_SCRIPT=none` records without driving the UI.

### QML settings

QML keeps state of its own in `Settings` (the app's default QSettings), e.g. "notifications
prompt already seen". The recorder snapshots them when it attaches (`settings.ini`), without
fallbacks (organization-wide settings, the macOS global domain) and keeping only unkeyed groups
plus the recorded profile's: groups named after any other `0x…` id (the developer's other
profiles, account addresses, communities) never go into a recording. The file is written
without any keyed group at attach and rewritten with the profile's (`userProfile.pubKey` and
`keyUid`) at the `loggedIn` moment. Replay
points its default QSettings at a fresh ini store in its temp dir and fills it from the
snapshot before QML loads, so replay starts from the same settings and never writes the
real app's.

### Calibration probe

`STATUS_REPLAY_PROBE_AT=<s>,<s>...` (record or replay) writes `probe/probe-<n>.tsv`
(recording), `replay-probe/probe-<n>.tsv` (replay) or, in script-only runs,
`$STATUS_REPLAY_PROBE_DIR`, else `probe/` in the Android package's external files dir (exported
as `STATUS_EXTERNAL_FILES_DIR`, readable with adb without run-as), else the app data dir, at those seconds after start: the composite QML types loaded and instantiated, the
malloc heap in use and (Linux/Android) PSS + SwapPss per mapping from the process's own smaps.
"Loaded" means the type loader holds the type's compiled unit; "instantiated" means the engine
created an executable unit for it, i.e. at least one object of it was created at some point
(not that one is alive now).

### Media

StatusQ's network managers expose two hooks (`statusq_setNetworkReplyTap`,
`statusq_setNetworkRequestInterceptor`). The recorder taps every reply and describes each
successful GET in `media/index.jsonl` (body peeked at when the reply finishes, so QML still
reads all of it), keyed by a SHA-256 of the URL the app requested (not where a redirect
ended); status-go's media server URLs (`localhost`/`127.0.0.1`, a new port every run) are
hashed without host and port. Only image headers are read (size, format, frame count; for GIFs
whether the first frame is transparent): recording runs on QML's image reader thread and does
not decode. A response the consumer drained before it finished (XHR reads as data arrives)
can't be described and is left out.

By default only metadata is kept: HTTP status, content type, size and, for images, pixel
size, image format, decoded pixel format and frame count. Replay serves a generated image
with the same size, format, pixel format and frame count (a solid GIF for GIFs and other
animations, which Qt cannot write; an empty SVG of the same size for SVGs), so decoded images
and textures cost what the originals did, and non-image responses as filler of the same size.
`STATUS_RECORD_MEDIA=bytes` keeps the real bodies and URLs instead, for visual checks: **such
recordings contain personal images** (avatars, chat images) and must stay local.
`STATUS_RECORD_MEDIA=0` leaves media out. Replay lets requests it has nothing recorded for
go to the network.

### Format (directory, versioned, diffable)

```
<recording>/
  manifest.json      formatVersion, app commit, Qt version, platform, DPR, startedAt,
                     setup fingerprint, moments [{name, seq, t, snapshot}], coverage gaps
  schemas/<hash>.json
  snapshots/<moment>.json   root context + every live object (props, model rows)
  timeline.jsonl     one event per line, {"seq","t","k",...}
  media/             index.jsonl describing recorded GET responses (+ bodies with STATUS_RECORD_MEDIA=bytes)
```

Values are JSON, untagged when the declared type is concrete (string, bool, int,
int64, double) and tagged when it is `QVariant` or a container: `{"$obj":17}`,
`{"$t":"QUrl","v":"..."}`, `{"$list":[...]}`, `{"$map":{...}}`, `{"$t":"bytes","v":
"<base64>"}`, `null` for an invalid variant. Snapshots are written with sorted keys and
indentation; the timeline is compact JSONL. The replayer refuses an unknown major
`formatVersion`. 1.1 keys media by hash; 1.0 recordings (plain keys) still replay.

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
- **Values read late**: a property keeps its value from the event that opened its
  segment (object init or a notify signal) until the next notify, so a recorded read is
  applied at the segment start: replayed QML may read earlier than the recorded QML did.
  An object referenced before its recorded creation (the recorder registers objects on
  first sight) is built early from its init state.
- **Player**: without `--moment`, starts from the beginning, applying everything up to
  the `load` event at once. A moment before the load starts from its snapshot. A moment
  after it is reached by playing the timeline up to it as fast as QML keeps up, because
  QML keeps state of its own (current page, popups) that only follows from the events
  that led there; `--from-snapshot` starts from the snapshot instead. Root context
  properties are set before loading `main.qml`. The timeline then plays in one of three
  modes: `realtime` (recorded timestamps, `--speed`; F10
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
| `make record-run [RECORDING=<dir>] [RECORD_DATADIR=<dir>] [RECORD_SCRIPT=<json>\|none]` | unattended: creates a test profile on first use, records login screen -> login -> wallet (see above) |
| `make replay-run RECORDING=<dir> [REPLAY_MOMENT=<name>] [REPLAY_MODE=realtime\|step\|frozen]` | builds StatusQ + `libStatusReplay` + `status-replay` + `resources.rcc` (no Nim, no status-go) and runs it |
| `make mobile-record-run [RECORDING=<dir>]` | Android, unattended like `record-run`: a debug build with `libStatusReplay` packaged but not linked (`RECORDER=1`); creates the test profile when the package has none (checked with run-as, or for non-debuggable packages by a short scripted run that must reach the login screen), records login screen -> login -> wallet in-process, pulls the recording |
| `make mobile-replay-run RECORDING=<dir> [REPLAY_MOMENT=..] [REPLAY_MODE=..]` | builds the Android replay variant (`BUILD_VARIANT=replay`: no `libnim_status_client`, `libstatus_stub`, status-go or service), pushes the recording and launches it |
| `MOBILE_REPLAY_PROFILE=release` (both mobile targets) | non-debuggable, profileable packages (record side pr-style, `RECORDER=1` keeps record mode via `-d:statusRecorder`; recorder APKs are `StatusRecord.apk` / `StatusPRRecord.apk`, and a build without `RECORDER=1` fails if it would package the recorder); always signed with the Android debug keystore (`MOBILE_REPLAY_DEBUG_KEYSTORE`, default `~/.android/debug.keystore`), never the production one. Use it for memory numbers: a debuggable package carries 14-26 MB of extra ART on both sides, so debug replay numbers don't line up with release ones |
| `make run-replay-tests` | host round-trip and parity tests for `libStatusReplay` |

Android packages: both targets build debug packages, so the installed names get a
`.debug` suffix: `app.status.mobile.record.debug` and `app.status.mobile.replay.debug` by
default (`MOBILE_RECORD_BUNDLE_IDENTIFIER`, `MOBILE_REPLAY_BUNDLE_IDENTIFIER`). Neither
collides with the regular packages. Options reach the app through `status-env.txt` in the
app's external files dir, which debug packages read at start (`STATUS_RECORD*` for
recording; `STATUS_REPLAY` plus `STATUS_REPLAY_<OPTION>`, e.g. `STATUS_REPLAY_MOMENT`,
for replay, since Android passes no arguments). The scripts start the app once on a
fresh install so that it creates that directory itself: files adb puts into a directory
adb created are not readable by the app. The replay variant skips the status-go stub
and service in `StatusQtActivity` (`BuildConfig.REPLAY_VARIANT`).

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

- Replaying hand-driven user input. Scripted recordings replay exactly (their clicks are
  `ui` events, performed in order by a single runner that only walks the item tree while a
  target is missing: up to 60 s while fast-forwarding, 5 s in real time, each counted from
  when it becomes current; a click whose target doesn't show is skipped, and logged, as soon
  as the next one's target shows up while it waits); a recording driven by hand (`RECORD_SCRIPT=none`) replays the Nim state,
  but QML-only state (current page, scroll position, popups) diverges where QML does not
  derive it from Nim.
- Driving the backend from replay: slot calls never change replayed state, they only
  return recorded values.
- Web content (browser section, dApps WebEngine pages), camera frames, keycard, iOS.
- OS state such as the notification permission. The scripts answer the notifications
  prompt the same way in every run ("Don't ask me again", then "Maybe later"), and replay
  repeats those clicks.
- Editing or synthesizing recordings beyond the test fixture.
- Exact async timing (status-go response latency, incubation interleaving) beyond the
  recorded timestamps. Realtime replay has no back-pressure when QML is slower than the
  recording (the play-up-to-a-moment path does).

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
- **Setup drift**: the replay setup duplicates Nim's Qt-side startup. The fingerprint
  records the values Nim passed (incubation budget, network cache size, import paths of
  the Nim QML libraries, env defaults; StatusQ exposes the first two through
  `statusq_incubationControllerSettings` and `statusq_networkAccessFactorySettings`), `make record-run` fails when
  `status-replay --check-setup` finds a difference, and SetupParityTest checks the
  replay setup against the committed fixture.
- **Private Qt API** (`QMetaObjectBuilder`, `QQmlContextData`) can change between Qt
  minors; both are confined to `libStatusReplay` and covered by the host tests.
- **Media**: only responses the recorded run actually loaded are served; anything else
  goes to the network (or fails offline), which can make image memory differ from a real
  run.
