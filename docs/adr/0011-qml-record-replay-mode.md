# Record Nim→QML state at the nimqml boundary, replay it from generic C++ objects

Status: accepted

The app gets a permanent record/replay mode: a recorder captures everything Nim hands
to QML, and a replayer runs the unchanged QML app from that recording with no Nim, no
status-go and no Android service process. Both live in a separate library,
`libStatusReplay` (`ui/replay/`), which normal builds do not link: the app `dlopen`s it
only when `STATUS_RECORD` is set, and the replay entry points link it.
Details in [replay-mode.md](../replay-mode.md).

The recorder observes the boundary, not the 225 Nim types: two nil-checked hooks in
nimqml-seaqt (context property set; post-dispatch metacall, for slot calls from QML),
plus generic `QMetaObject` / `QAbstractItemModel` walks and per-object signal
connections from C++. The replayer rebuilds each recorded metaobject with
`QMetaObjectBuilder` on a generic object or list model and serves recorded values,
signals, model operations and slot returns. A recording is a versioned directory:
manifest, schemas keyed by content hash, per-moment snapshots, and a JSONL timeline.

## Why not the obvious paths

- **Instrument each Nim module / call site** — 49 context-property sites, 1926 slots and
  80 models; any new property or slot would silently fall out of the recording. One hook
  at the dispatch layer covers new code with no extra work.
- **Record above QML (QML debugger / profiler services)** — they see bindings and QML
  objects, not native slot return values or model rows, and need the debug server on.
- **Stub stores in QML for replay** — breaks the rule that QML does not change for replay,
  and stubs drift from the real backend surface; storybook already covers stub-driven
  component work.
- **Recorder inside StatusQ** — every normal run would map its code and pull in the
  Qt private modules it needs; a `dlopen`ed library costs nothing unless recording.
- **One shared setup entry for app and replay** — would mean refactoring the Nim startup
  for a dev tool; a duplicated replay setup plus a fingerprint check catches drift
  without touching it.
- **Replay by reusing the Nim objects with a fake status-go** — keeps libnim, the Nim
  runtime and its memory in the process, which is what the memory work needs removed, and
  status-go's API surface is far larger than the QML-facing one.
- **Class name as schema key** — many Nim types share a name (`View`, `Model`); a content
  hash keeps them distinct.

## Consequences

- nimqml-seaqt gains two optional C hooks (local branch now, upstream seaqt PR and pin
  bump later); cost when off is one branch per metacall, and no added pages.
- The replayer duplicates the Qt-side part of the Nim startup. Each recording stores a
  setup fingerprint of the real app; replay diffs against it and a host test asserts no
  mismatches, so a change on one side fails until the other follows.
- Replay fidelity is bounded by what the recording saw: slots QML calls with unrecorded
  arguments return the latest or a typed default, and every miss is logged.
- Recordings contain account data; they live in the gitignored dev data dir and are
  never committed. Sensitive slot arguments are redacted at record time.
- `QMetaObjectBuilder` and `QQmlContextData` are Qt private API, confined to
  `libStatusReplay` and covered by host round-trip tests.
