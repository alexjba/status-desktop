# s-qmltc: does qmltc cut the fixed floor? (#21528 / #22657)

Status: FINAL (2026-11-08). **No.** Stopped at gates 1+2. No worktree, no branch, no device runs.

## TL;DR
- qmltc does not replace runtime type data. It adds to it.
- Heap: +47 % on first load (+40 KB per type) and +4 % per instance.
- Code: +118 KB of `__text` per type.
- On top of that, a 6.11 crash and the cross-module block.

## Why (Qt 6.11 source)
- Every qmltc document root's generated `__qml_init__` calls `createInternalContext(compilationUnitFromUrl(url()), ...)`.
  - Generator: tools/qmltc/qmltccompilerpieces.h ~243.
  - Bindings, functions and `Component` creation also call `compilationUnitFromUrl` (qmltccompiler.cpp 1300, 1358, 1659, 1982).
- `QQmlEnginePrivate::compilationUnitFromUrl` (qqmlengine.cpp 1976) calls `typeLoader()->getType(url)`, which runs the full `QQmlTypeData` pipeline:
  - imports and typeNameCache;
  - `checkDependencies`, which recursively loads every composite dependency;
  - `createTypeAndPropertyCaches` (qqmltypedata.cpp 578).
- So the heap type data that exists today is still built for every qmltc file.
- In addition, the static metaobjects get C++ property caches the first time QML touches them, and every type adds generated code.

## Mac probe (toy, Qt 6.11.0 macOS, Release, offscreen)
Setup:
- 100 types, each one an Item with 18 props, 1 signal, 2 functions, a handler and 6 Rectangles with bindings and a function.
- A Root.qml instantiates all 100. 700 objects.
- Same binary for both modes, with `ENABLE_TYPE_COMPILER`: `comp` = QQmlComponent on the qmlcachegen AOT unit, `tc` = `new Bench::Root(&engine)`.
- Measured with `malloc_zone_statistics`, n=3. Repeats are within ±12 KB.

| | comp | qmltc | Δ |
|---|---:|---:|---:|
| first instance, type data included (KB) | 8,555 | 12,556 | **+4,001 (+47 %)** |
| second instance, cached types (KB) | 1,817 | 1,895 | +78 |
| qmltc root created after comp already loaded all types (KB) | — | 6,325 | ≈ +4.4 MB beyond the type data that is already shared |

- The third row confirms the extra: when the QQmlTypeData is already cached, the first qmltc instance still costs about 4.4 MB more than a cached instance.

Binary (macOS arm64, same 100 files):

| | qmlcachegen only | + qmltc | Δ |
|---|---:|---:|---:|
| `__text` | 2.33 MB | 14.11 MB | **+11.8 MB (~118 KB per type)** |
| `__DATA_CONST` (relocated const, the analogue of .data.rel.ro) | 0.30 MB | 0.99 MB | +0.68 MB (dirty pages) |
| `__DATA` | 0.07 | 0.16 | +0.09 |

- Generated code: 3.8 k C++ lines for a 25-line QML file. The 100 files produce 569 k lines including moc.
- The qmlcachegen units stay; qmltc needs them for bindings and functions.

Gate 2 is clearly failed: the heap goes UP by 4 MB per 100 types, and the .so grows about 12 MB.
- Scaled to the login closure: roughly 100–150 types at the login screen and about 460 instantiated after visits.
- That is +4..6 MB heap and +12..18 MB code at login, and much more post-login.

## Feasibility (gate 1): also blocked
- **qmltc + AOT crash (6.11).** A binding that calls an own QML function (`width: sum()`) segfaults in `QMetaObject::method` from the AOT lookup (`_qt_qml_Bench_src_T0_qml::$_3`).
  - The AOT method index comes from the type-data property cache and does not match the static metaobject.
  - The same QML works through QQmlComponent. Our code base does this pattern everywhere.
  - Repro: toy/src-withcall.
- **Cross-module composites are refused** (qmltcvisitor.cpp 786).
  - The April 2026 run: 116 of 947 files compile, 831 fail.
  - Patching the check out does not help. qmltc then has to emit `#include` and a C++ base class for StatusQ and for **QtQuick.Controls.Basic's QML-defined controls**, which have no qmltc headers. So Qt's own Basic style would have to be qmltc-compiled too, and the Qt docs say this is not supported ("only QtQml/QtQuick or C++-only modules").
- **qmltc only works for roots created from C++.** Our login screen is reached through Loaders and by-URL loading from AppMain/main.qml, and those stay on the engine path.

## Verdict
qmltc is a startup and binding-speed tool (static metaobjects, direct C++ property access). It is not a memory lever. In Qt 6.11 it adds both heap and code. Drop it.

## Files
- Probe: spikes-floor/qmltc/toy/{gen.py,main.cpp,CMakeLists.txt}.
- Builds: toy/build-ON (qmltc), toy/build-OFF (qmlcachegen only).
- Crash repro sources: toy/src-withcall.
