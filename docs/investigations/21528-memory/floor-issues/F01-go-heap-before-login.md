# Service process: 20MB of Go heap before any profile is opened

Context: status-im/status-app#21528. At the login screen the Android service process holds 20.1MB of Go heap plus
2.3MB of immortal runtime metadata (smaps `[anon: Go: heap]`, Redmi A5, 2026-10-02) before `Login`/`CreateAccount`
is called. Nothing user-visible runs yet, so this is init-time state: package `init()` tables, the embedded default
token lists (3.3MB `[]byte` literal copied to heap), and whatever `InitializeApplication` builds. It persists for the
process lifetime and sits under the GC heap, so it also inflates GC headroom after login.

## Findings (host reproduction, 2026-10-02)

Method: a throwaway `cmd/prelogin-heap` main (source kept at the session scratchpad `f01/main.go`, not committed)
links `package mobile` from `vendor/status-go` @ `6bd2973d3797` with the Android tags
(`gowaku_no_rln nowatchdog disable_torrent`), runs the pre-login sequence against a temp data dir and writes a heap
profile + `runtime.MemStats` after package init, after `InitializeApplication` and after `ConnectionChange`.
Attribution runs used `GODEBUG=memprofilerate=1` (every allocation sampled). Host: darwin/arm64, go1.26.0.
Raw outputs: `profiles/floor/f01-memstats.txt`, `f01-heap-after-init-top.txt`, `f01-heap-after-init-top-cum.txt`,
`f01-heap-after-init-by-package.txt`, `f01-initialize-application-delta.txt`, `f01-heap-after-init-churn-and-regexp.txt`.

### Pre-login call sequence into status-go

Shared Nim path (desktop, iOS, and the Android UI process through the Binder stub):

1. `src/nim_status_client.nim:126` `status_go.setSignalEventCallback` → `mobile/status.go:942` (stores a pointer).
2. `src/app/boot/app_controller.nim:387` `onboardingModule.load()` → `src/app/modules/onboarding/module.nim:174`
   `buildLoginAccountsModel` → `:140` `controller.getOpenedAccounts` → `src/app/modules/onboarding/controller.nim:187`
   → `src/app_service/service/accounts/service.nim:117` `openedAccounts` → `src/backend/accounts.nim:290-304` builds
   the payload (`logEnabled: true`, `logLevel`, `apiLoggingEnabled`, `metricsEnabled`, `wakuFleetsConfigFilePath`; the
   last three are false/empty by default) and calls `status_go.initializeApplication` (`src/status_go.nim:14`) →
   `vendor/status-go/mobile/status.go:63` `InitializeApplication`.
3. `InitializeApplication` (`mobile/status.go:81-156`): `initializeLogging` (`:158`, zap root logger; request log and
   Prometheus server only when enabled), `MkdirAll`, `SetMediaServerOptions` (`:102`), `UpdateRootDataDir`,
   `OpenAccounts` (multiaccounts sqlcipher DB + migrations), `GetAccounts`, `SetSentryDSN`, fleet config files, media
   URLs for account images.
4. Nothing else reaches status-go before `Login`; `ConnectionChange` on desktop is post-login
   (`src/app/modules/main/controller.nim:582`).

Android service process specifics:

- `StatusGoService.onCreate` (`mobile/android/qt6/src/app/status/mobile/ipc/StatusGoService.java:599-605`) →
  `nativeInit` → `mobile/statusgo_service/statusgo_service_jni.cpp:130` `SetSignalEventCallback`, then
  `registerNetworkCallback` (`:496`) whose first callback fires immediately → `dispatchConnectionChange` (`:463`) →
  `nativeCall("ConnectionChange")` (`:476`) → `mobile/status.go:1994`. So the service sends `ConnectionChange` before
  login on its own.
- UI-process Nim calls travel `mobile/statusgo_stub/statusgo_stub.cpp:93` (Java `StatusGoStub.call`) → Binder →
  `StatusGoService.java:538` `nativeCall` → `statusgo_service_jni.cpp:143` `statusgo_service_dispatch` (generated
  `vendor/status-go/build/bin/statusgo_service_dispatch.cpp`) → `mobile.InitializeApplication`.
- Profiling builds only: `StartPprof` and `PausableServices` (`StatusGoService.java:365,400`) run on the first UI
  visibility change, not necessarily before login; iOS/desktop `goPprof` builds call `StartPprof` at
  `src/nim_status_client.nim:283`.

### MemStats (host, MB; `f01-memstats.txt`)

| point | HeapAlloc (live, after GC) | HeapInuse after GC | HeapInuse as found (no forced GC) | HeapObjects |
|---|---|---|---|---|
| after package `init()`, before any call | 13.6 | 16.4–17.1 | 16.5–17.6 | 74.8k |
| after `InitializeApplication` | 13.7 | 16.4–16.8 | 19.3–19.8 | 75.4k |
| after `ConnectionChange` | 13.7 | 16.4–16.8 | 17.7–18.2 | 75.5k |

- 99.5% of the live pre-login heap is created by package `init()`; `InitializeApplication` retains 67kB (media server
  TLS cert + http mux, sqlcipher multiaccounts DB handles, json encoder caches) and churns ~3MB (migrations bindata
  gunzip, sqlcipher open).
- The device's 20.1MB `[anon: Go: heap]` is consistent with host "as found" after `InitializeApplication`:
  13.7MB live + ~3MB allocator slack (HeapInuse − HeapAlloc: size-class rounding and partially filled spans) + ~3MB of
  not-yet-collected garbage. `NextGC` after init is 26–28MB, so the runtime does not collect that garbage until the
  heap grows past it after login. Only the 13.7MB live part is addressable by code changes; slack and garbage scale
  with it.
- Allocation churn during package init is 28.5MB for 13.7MB retained (`TotalAlloc`), i.e. init already pays for ~6–9
  GC cycles and the peak heap during startup is higher than the settled number.

### Attribution of the live heap after package init (14.0MB profile total)

Per package `init` function (cum, `f01-heap-after-init-by-package.txt`), classified:

| MB | owner | what it is | class | how to get it back |
|---|---|---|---|---|
| 4.0 | `github.com/status-im/zxcvbn-go/matching` | `buildRankedDict`: `map[string]int` per frequency list (`matching.go:20` eager `init()`) | lazy-able | only used by `GetPasswordStrength` (`mobile/status.go:1361`, onboarding password screen). `Omnimatch` already lazy-loads when the matchers are nil (`matching.go:28`); drop the `init()` in our fork. |
| 2.3 | `github.com/status-im/zxcvbn-go/frequency` | `[]string` word lists json-decoded from gzipped bindata (`frequency.go:19`); `reflect.growslice` 1.6MB + `literalStore` 0.7MB | lazy-able | same change in the fork: decode on first `Omnimatch`; the gzipped bindata stays in rodata (clean pages) |
| 3.25 | `pkg/services/wallet/token/local-token-lists/default-lists` | 8 `[]byte{0x.., …}` composite literals copied to heap in `init()` (`coingecko_ethereum.go:7` etc.; ethereum 1.0, bsc 0.7, uniswap 0.6, base 0.5, arbitrum 0.2, status 0.14, optimism 0.05, linea 0.02) | lazy-able / removable from heap | consumed only by `initialListProviderFromEmbedded` (`token/token.go:173`) after login. Store as `go:embed` files or `string` constants: the bytes stay in `.rodata` (file-backed, clean, already in the binary today) and the heap copy disappears; decode-on-use is unchanged. |
| 1.08 | `go-playground/validator` v10 (0.79, via `go-wallet-sdk/pkg/tokens/types`) + v9 (0.29, status-go `params`, `requests`, `networks`, `pairing`) | ~60 package-level `regexp.MustCompile` each (v10.11.1 and v9.31.0 both eager; v10 ≥ 10.22.0 compiles lazily, verified in v10.22.0 and v10.30.5 `regexes.go`) | lazy-able | bump v10 in go-wallet-sdk; migrate status-go off v9 to the same v10 (also removes a duplicate copy) |
| 0.48 | `brianvoe/gofakeit/v7` (+`/data`) | fake-data tables | removable | only `Contact.Fake` (`internal/protocol/contacts/contact.go:577`) and `Community.Fake` (`communities/community.go:2728`), test helpers compiled into production; move behind a build tag or into test packages |
| 0.27 | `go-ethereum/core/vm` | per-fork jump tables built in `init` | removable with F04 | status-go imports it only for error values (`healthmanager/provider_errors.go:132-141`, `rpc/chain`); but `go-ethereum/core` ← `node` ← `pkg/services/personal`, `pkg/services/ext` keeps it linked until `node` goes |
| 0.14 | `russross/blackfriday/v2` | markdown tables | removable with F04 | ← `go-md2man` ← `urfave/cli` ← `go-ethereum/internal/debug` ← `go-ethereum/node` |
| 0.14 | `go-ethereum/metrics` | meters/samples created by go-ethereum package vars (`rawdb`, `pathdb`, `snapshot`, …) even with metrics disabled | inherent while those packages are linked | see metrics note below |
| 0.42 | protobuf descriptors (`protocol/protobuf` 0.17, `protodesc` 0.14, `impl` 0.13, `descriptorpb` 0.04, otlp 0.04) | message/file descriptors registered at init | inherent | protobuf already lazy-inits most of it; not worth touching |
| 0.24 | `runtime` (`schedinit`, `procresize`) | per-P mcache etc. | inherent | |
| ~1.5 | long tail (<0.07 each: `go-multicodec`, `pathdb`, `snapshot`, `go-waku/node`, `callog`, `gojsonschema`, `btcd/wire`, fleets, contracts, migrations, grpc, dns, …) | mostly small regexes and maps | inherent / F04 | `regexp` objects total 1.46MB across all packages; after the validator fix ~0.4MB remains |

Sums: lazy-able 10.6MB (zxcvbn 6.3 + token lists 3.25 + validator 1.1), removable 0.9MB (gofakeit 0.5, core/vm +
blackfriday 0.4 with F04), inherent ~2.5MB. The 2.6–3MB allocator slack shrinks roughly in proportion.

### go-ethereum metrics link flag

`-X github.com/status-im/status-go/vendor/github.com/ethereum/go-ethereum/metrics.EnabledStr=true` (Android and iOS
`Makefile:466,477`) is a no-op: go-ethereum v1.16.3 has no `EnabledStr` symbol (metrics are enabled only through
`metrics.Enable()`, which nothing in status-go or the linked go-ethereum packages calls; `cmd/utils/flags.go` is not
linked) and the repo is not vendored, so even the path is wrong. Measured: `metrics.Enabled()` is `false` with the
Makefile flag, with the corrected path and without any flag; binaries differ only in build info. Metrics on/off delta:
0MB. The 0.14MB of `go-ethereum/metrics` heap is allocated by package vars regardless of the flag. Remove the flag for
clarity; there is no memory to win there.

### Ranked removals (estimates; host live heap, Android should match within ±10%)

1. zxcvbn lazy load (fork change: delete `matching.init`, make `frequency.Lists` a lazy getter): −6.3MB live, plus its
   ~10MB of init churn (`buildRankedDict` 7.9MB alloc) off the startup path. Password strength is still computed on
   first use; first call pays ~50ms (estimate) of decode.
2. Token lists as `go:embed`/string constants: −3.25MB live, −3.25MB of init copy churn, no runtime change (decode on
   first wallet use as today).
3. validator v10 bump in go-wallet-sdk + v9→v10 migration in status-go: −1.1MB live, −1.1MB churn.
4. gofakeit out of production builds: −0.5MB live (and ~0.3MB text, F04 counts it too).
5. With F04 (drop `go-ethereum/node`): −0.4MB live (core/vm, blackfriday) on top of the text savings.

Expected pre-login live heap after 1–4: ~2.8MB instead of 13.7MB; `[anon: Go: heap]` at the login screen estimated
8–9MB instead of 20MB (live + slack + the same ~3MB of InitializeApplication garbage, which a one-off
`debug.FreeOSMemory()` after `InitializeApplication` would also return; cheap, ~5ms, worth measuring on device).

### Caveats

- Host darwin/arm64, not Android; same build tags and the same package set (`go list -deps` of `package mobile`), but
  the runtime's arena/page behaviour and the kernel's RSS accounting differ. The 20.1MB device figure was reconciled
  by composition (live + slack + uncollected garbage), not re-measured; the acceptance ledger row is still needed.
- `SetSignalEventCallback` was not called in the probe (needs a C pointer); it stores one pointer.
- The probe's own first profile write adds ~1MB to `TotalAlloc` between the first two snapshots in the default-rate
  runs; the `memprofilerate=1` run inflates `BuckHashSys`/`Sys` (profiler buckets), its heap numbers are unaffected.
- Not measured: `CreateAccount`/`Login` paths, the Android `PushNotificationHelper.initialize` and notification
  manager (Java side, not Go heap).

## What to build

1. The removals above, each as its own commit with the before/after `HeapAlloc`/`HeapInuse` from the probe in the
   message: zxcvbn lazy init (fork), token lists as `go:embed`, validator v9→v10 + bump, gofakeit behind a build tag,
   drop the dead `-X …metrics.EnabledStr` link flag.
2. A device ledger row at the login screen before and after: Go `HeapInuse`/`HeapAlloc` from the pprof `heap`
   endpoint (profiling build, `StartPprof` before login) and `[anon: Go: heap]` PSS from smaps, warm and cold page
   cache.
3. Measure a one-off `debug.FreeOSMemory()` at the end of `InitializeApplication` on device (expected −3MB of
   garbage at the login screen; keep only if login duration does not regress).
4. The attribution table above goes into `docs/investigations/21528-memory/floor.md`; the rest (core/vm,
   blackfriday, protobuf, runtime) is inherent or belongs to F04.

## Acceptance criteria

- [ ] A ledger row shows Go `HeapInuse`/`HeapAlloc` and `[anon: Go: heap]` PSS at the login screen before and after, same device, warm and cold page cache
- [ ] Host probe shows live heap after package init ≤ 4MB (from 13.7MB); each removal commit quotes its delta
- [ ] The attribution table (owner → MB → classification) is in `docs/investigations/21528-memory/floor.md`
- [ ] Password-strength result unchanged on the onboarding screen; wallet token lists unchanged after login
- [ ] No regression in time-to-login-screen or login duration (CPU rows in the ledger)

## Blocked by

Nothing; the profiling build with early pprof exists on `perf/21528-memory-footprint`. The zxcvbn change needs a
commit on the `status-im/zxcvbn-go` fork and a go.mod bump.
