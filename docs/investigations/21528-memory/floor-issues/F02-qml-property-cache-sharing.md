# QML engine: 13MB of property-cache copies at the login screen, ~25MB after login

Context: status-im/status-app#21528. heapprofd on the UI process at the login screen (Redmi A5, 2026-10-02):
13.3MB live under `QQmlPropertyCache::copyAndReserve` called from `QQmlTypeLoader::setCachedUnit` →
`QQmlDataBlob::tryDone`, i.e. one full flattened property table per QML type and per inner object that declares
properties (each `QtObject { id: d; property … }` is a type). With ~250 QML files loaded before login this is the
largest single heap owner; the engine heap grows from 33MB to 49MB after login as more types load. Qt's design copies
the parent's table (an `Item` subclass carries ~130 inherited entries, a `Text` subclass ~200) into every derived
cache; the caches are never freed.

CI builds Qt from source (`ci/Jenkinsfile.qt-build`, image `/opt/qt/6.11.0`), so a Qt patch is deployable.

## What to build

1. Measure the count and size distribution of property caches at the login screen and at settle (a debug hook or
   a gdb/lldb script over `QQmlMetaType`'s cache table), to confirm the per-type cost and how many are inner objects.
2. Prototype in our Qt build: share the parent's `QQmlPropertyData` vector between a cache and its derived caches
   (copy-on-append instead of copy-on-create), or hash-cons identical tables. Measure the delta at login and settle,
   and CPU for property lookup (`QQmlPropertyCache::property` by name) with the storybook benches.
3. App-side counterpart, independent of the patch: fewer inner objects with declared properties in hot base
   components (`StatusQ` controls), flatter inheritance chains where a layer adds no properties.
4. If the patch holds, upstream it to Qt and carry it in `Jenkinsfile.qt-build` until it lands.

## Acceptance criteria

- [ ] Count/size table of property caches at the login screen and settle, before and after
- [ ] Ledger rows: UI PSS at login screen and settle, unreclaimable column, with the patched Qt
- [ ] No CPU regression on the storybook load benches or in login duration
- [ ] Patch submitted upstream with the measurements, or a written reason why not

## Blocked by

Nothing for the measurement and the app-side part; the Qt patch needs a Qt rebuild on CI.
