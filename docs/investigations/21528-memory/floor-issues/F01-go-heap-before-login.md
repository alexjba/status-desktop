# Service process: 20MB of Go heap before any profile is opened

Context: status-im/status-app#21528. At the login screen the Android service process holds 20.1MB of Go heap plus
2.3MB of immortal runtime metadata (smaps `[anon: Go: heap]`, Redmi A5, 2026-10-02) before `Login`/`CreateAccount`
is called. Nothing user-visible runs yet, so this is init-time state: package `init()` tables, the embedded default
token lists (3.3MB `[]byte` literal copied to heap), go-ethereum metrics registries (forced on by
`-X …metrics.EnabledStr=true` in the Android link), and whatever `InitializeApplication` builds. It persists for the
process lifetime and sits under the GC heap, so it also inflates GC headroom after login.

## What to build

1. A heap profile of the service at the login screen: the profiling build starts the pprof server from the Nim
   `mainProc` (`StartPprof`) before login; take `heap` (inuse_space) and `allocs` through `iproxy`/`adb forward`.
2. Attribute the 20MB to owners (package-level) and classify each as: removable (dead on mobile), lazy-able (build on
   first use), or inherent.
3. Land the cheap removals in this issue (metrics off on mobile, `go:embed` or lazy decode for the token lists,
   init tables built on first use); file the rest as separate issues with the measured size.

## Acceptance criteria

- [ ] A ledger row shows Go `HeapInuse` and `[anon: Go: heap]` PSS at the login screen before and after, same device, warm and cold page cache
- [ ] The attribution table (owner → MB → classification) is in `docs/investigations/21528-memory/floor.md`
- [ ] No regression in time-to-login-screen or login duration (CPU rows in the ledger)
- [ ] Each removal is its own commit with the heap delta in the message

## Blocked by

Nothing; the profiling build with early pprof exists on `perf/21528-memory-footprint`.
