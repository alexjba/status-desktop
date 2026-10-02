# QML convention: feature modules referenced by URL and trimmed on close (pilot: popups)

Context: status-im/status-app#21528. Each QML type the engine loads costs heap for the process lifetime: its
regenerated compilation unit, a full property cache per type and per inner object, and the type registration
(~18MB for the ~250 files loaded before login; 49MB engine heap after login). Qt cannot unload a type that is still
referenced from a loaded document, but `QQmlEngine::trimComponentCache()` releases compilation units and types that no
live object references. Today heavy subtrees are reachable statically: a static type reference (`Foo {}` or
`sourceComponent: Foo {}`) in an always-loaded file loads `Foo` and everything it references when the parent file
loads, even if the object is never created. Sections already load by URL (`AppMainLoader`, the section loaders);
popups and settings subpages do not.

## What to build

1. Convention, in `guidelines/QML_ARCHITECTURE_GUIDE.md`: heavy or rarely used subtrees (popups, settings
   subpages, sections) are referenced only by URL (`Loader.source`, `Qt.createComponent(url)`), never as static types
   from files that are always loaded; after such a module closes, the owner calls a central `trimComponentCache()`
   hook (debounced). A qmllint/CI check flags static references to files under the designated module roots.
2. Pilot on popups: `PopupsLoader`/`HandlersManager` reference each popup by URL; measure engine heap and PSS at
   settle before/after, and after open+close of the five largest popups with and without trim.
3. Report CPU: first-open latency of each popup by URL vs static (the storybook popup benches), to size the trade-off.

## Acceptance criteria

- [ ] Guideline section merged, with the module roots named
- [ ] Pilot PR: ledger rows (settle PSS, engine heap via heapprofd) before/after; open+close+trim rows for 5 popups
- [ ] Popup first-open latency table; any regression above the bench gate is addressed before merge
- [ ] Lint check in CI for static references into the module roots

## Blocked by

Nothing.
