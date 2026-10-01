# #21528 — Android memory footprint (fresh profile baseline)

Issue: https://github.com/status-im/status-app/issues/21528. Target: ~350MB for UI + service process
on a fresh, empty profile at the settle point. Scope of this investigation: the **fresh-profile
baseline** only; scaling with account data is tracked separately (#21558).

## Contract

- **Memory footprint** = sum of PSS of the UI process (`app.status.mobile`) and the service process
  (`app.status.mobile:statusgo`) from `dumpsys meminfo`. On Android 15 `TOTAL PSS` includes SwapPss.
- **Settle point** = fresh profile, landed on the default (wallet) section, foreground, screen on,
  untouched for 1–2 minutes.
- **Pre-login floor** = the footprint at the login screen before any profile is loaded.
- Reference device: Redmi A5 (`25028RN03Y`, Android 15, 2.8GB RAM, `ro.config.low_ram=true`, 3GB zram).
  The reporter's Realme Note 50 (4GB) is a cross-check only.
- Baseline build: the release APK installed on the device (master, 2026-09-02). Drills run on the
  `profile` gradle build type (`profileable`, distinct `.debug` application id).

## Tooling

- `mobile/scripts/profiling/memsnap.sh <label>` — snapshots both processes into `mem/ledger.csv`
  (PSS, RSS, SwapPss, native heap, EGL, .apk/.so mmap, Unknown = Go/Nim mmap arenas, VmRSS/VmSwap/threads).
- `make mobile-profile` — profileable build; QML debugger no longer blocks unless `QML_DEBUG_WAIT=1`.
  The `:statusgo` process auto-starts pprof on `127.0.0.1:6060` in non-release build types
  (`adb forward tcp:6060 tcp:6060`; `curl 'http://127.0.0.1:6060/debug/pprof/heap?debug=1'` ends with
  `runtime.MemStats`).
- UI-process native heap: Android Studio Profiler → Memory → *Native allocations* (heapprofd) on the
  profile build.

## Ledger — release build 2026-09-02, Redmi A5 (PSS incl. swap, MB)

| run | UI | statusgo | total | notes |
|---|---|---|---|---|
| login screen (3 runs) | 240–272 | 74–102 | ~330 | pre-login floor |
| fresh onboarding → wallet, T+5min | 422 | 362 | 785 | Go heap oscillated 240–340MB |
| warm login (2 runs), T+2min | 415–422 | 569–583 | ~1000 | Go heap flat at 488MB (limit 500MB) |
| warm login, third-party + RSS off | 356 | 555 | 911 | Go heap unchanged (475MB) |

Bucket detail per sample: `ledger/release-sep02-redmi-a5.csv` and the raw `dumpsys meminfo` dumps.

Observations from the ledger alone:
- The pre-login floor is ~330MB: UI native heap ~100MB, ~90MB of mapped code pages (`.apk mmap`,
  the 292MB uncompressed native-lib payload loaded straight from the APK), 12.5MB EGL, plus ~75MB
  for the service process (mostly libstatus.so pages + Go runtime).
- Logging in adds ~140MB UI native heap and ~250–450MB of Go/Nim anonymous memory.
- Disabling all third-party services (wallet workers, token refresh, market, news) removes ~65MB
  from the UI process and **nothing** from the service process → the service-process cost is on the
  messenger side (pinned community bootstrap, curated-directory store fetches, community JSON,
  libsds), plus GC headroom (GOGC=100 under a 500MB soft limit).
- ~100MB of the UI native heap gets swapped to zram within 5 minutes of idling in the foreground:
  allocated-but-untouched data, not working set.

## Service process: Go runtime at the settle point (profile build, fresh profile)

`gosnap.sh` MemStats ~4 min after onboarding (`profiles/statusgo-heap-settle-*`):

| MemStats | MB | meaning |
|---|---|---|
| HeapAlloc | 67 | live objects |
| HeapInuse | 80 | spans holding live objects |
| HeapSys | 689 | heap high-water mark (startup burst) |
| HeapReleased | 584 | returned to the OS |
| Sys | 705 | all Go-managed address space |

So Go's *live* data is small. What the ledger sees as 250–490MB of anonymous memory is the
**startup burst** (heap grew to ~690MB during onboarding/login) plus **idle heap** the background
scavenger has not returned yet: in the heapprofd run the service process dropped from 302MB to
164MB of anonymous memory between T+60s and T+120s with no code change. Go on Linux/Android uses
`MADV_DONTNEED` by default (runtime `madvdontneed=1`), so once scavenged the pages really leave RSS.
libsds (Nim refc heap, mmap-backed, also in "Unknown") and cgo malloc (sqlcipher, 7.6MB via
heapprofd) are minor.

**Correction after measuring:** Go on Android advises freed pages with `MADV_FREE`, not
`MADV_DONTNEED` — `runtime1.go` only defaults `madvdontneed=1` when `GOOS == "linux"`, and the
runtime's `GOOS` is `android`. With `MADV_FREE` the kernel keeps the pages resident (and every
meter keeps counting them) until it is under memory pressure. That is why HeapReleased was 346MB
while `dumpsys` still showed the full HeapSys, and why the number only moved when zram pressure hit.

Experiment (`ledger/profile-build-redmi-a5.csv`, rows `linkname-*`): flipping the runtime's advice
to `MADV_DONTNEED` at init (`mobile/madvise_android.go`, `//go:linkname runtime.adviseUnused`;
`//go:debug` refuses the unregistered setting and an env var set from Java never reaches the
runtime, which copies the loader's original `envp`):

| build | statusgo PSS at T+2min | Go anon ("Unknown") | HeapSys − HeapReleased |
|---|---|---|---|
| profile build, baseline (3 runs) | 354 / 392 / 569 | 278 / 302 / 477 | 105–146 |
| profile build, MADV_DONTNEED | **219** | **120** | 120 |

Total footprint drops from ~1000MB to ~655MB with no change to live data. Fix PR: status-im/status-go#7864 (`mobile/madvise_android.go`); upstream Go proposal draft: [upstream-go-issue-draft.md](upstream-go-issue-draft.md). The startup burst still
happens (HeapSys 496MB) but leaves RSS as soon as the scavenger runs. `FreeOSMemory()` after login
is only worth adding on top if the scavenger's own pace proves too slow; it did not in this run.

Cumulative allocations since start (`profiles/statusgo-allocs-settle-top.txt`, 4.7GB in ~5 min):

| site | GB | note |
|---|---|---|
| `argon2.initBlocks` (puzzleauth solver) | 0.75 | RPC-proxy proof-of-work; memory param comes from the server, fresh blocks per nonce attempt |
| `scrypt.Key` (keystore envelope KEK, N=2^18) | 0.57 | ~256MB per derivation, twice at profile creation, once per login |
| `wallet/common.MandatoryTokens` via `Reader.balancesToTokensByAddress` | 1.17 | called inside the address × token loop (`reader.go:244`); rebuilds and formats the whole mandatory list each iteration |
| coingecko `getCoingeckoTokensByTokenKey` | 0.46 | |
| `encoding/json.Marshal` / protobuf unmarshal | 0.31 / 0.30 | community + token payloads |

Live heap top (45MB sampled): JSON decode literals 4.6MB, `io.ReadAll` 4.4MB, goleveldb memdb 4MB,
zxcvbn ranked dictionary 3.7MB, token-list parser 7MB cumulative, embedded token lists ~4MB.

## UI process: native heap by owner (heapprofd from process start, profile build)

`heapprofd_startup.sh` + `heapprofd_report.py` (`profiles/ui-heapprofd-startup-report.txt`,
trace `profiles/heapprofd-startup-fresh-profile.sym.pftrace`). Sampled at 8KB; totals reconcile
with dumpsys "Native Heap" (≈100MB at the login screen, ≈200MB after login). The ART rows are
Java allocations without frees and are not part of the native heap.

| owner | login screen | after login (T+30s) | what |
|---|---|---|---|
| libQt6Qml | 33MB | 49MB | QQmlPropertyCache copies (~10MB), compiled units, bindings; ~4MB of QML compiled **at runtime** (`QmlIR::QmlUnitGenerator::generate` under `loadWithCachedUnit`) → some cached units are rejected and re-compiled from source |
| libQt6Core | 31MB | 40MB | `FontLoader` → `addApplicationFont` reads every font file into a QByteArray (7MB) + FreeType face copies; QByteArray/QArrayData growth |
| Mali GPU driver | 18MB | 45MB | host-side allocations from `glClear`/`QRhi::endFrame` (~18MB), `glTexSubImage2D` staging (4MB), `eglCreateContext` (3.4MB) |
| Nim (libnim_status_client) | 1MB | 50MB | `asyncCommunitiesDataLoaded` deep-copies (`=dup`) the CommunityDto with all chats: **32MB for the pinned Status community alone**; token catalogue `createTokenItem` in `asyncRefreshTokensTask` ~13MB; misc DTOs |
| libQt6Gui / Quick | 7MB | 26MB | decoded images (token icons via network → `convertToFormat`, SVGs), text layout, scene graph |

## Findings → candidate fixes (unordered; each needs its own before/after ledger row)

Service process
1. `Reader.balancesToTokensByAddress` calls `walletcommon.MandatoryTokens()` inside the
   address × token loop (`pkg/services/wallet/reader.go:244`): 1.17GB of garbage in 5 min. Build the
   set once (package-level `sync.Once`) and look up. **Done: status-im/status-go#7865** — GC cycles
   82→33, mallocs 45M→5.9M, non-KDF allocations 3.74GB→0.95GB in a comparable 5-min window; PSS
   unchanged (CPU/GC fix, as predicted). Ledger rows `hoist-*`/`hoist2-*`.
2. Release the startup burst: call `debug.FreeOSMemory()` once after login/onboarding settles
   (or temporarily lower the memory limit during login). Cheap, mobile-only.
3. Keystore envelope KEK scrypt at N=2^18 (~256MB per derivation, `keystore/envelope/scrypt_params.go`):
   the single largest spike at login. Any change is a security decision (see open questions).
4. RPC-proxy puzzle (`puzzleauth/solver.go`): argon2 with server-chosen memory, fresh block set per
   nonce attempt (768MB cumulative). Consider a cap / reuse / lower server params for mobile.
5. Community JSON repeats the member map per token-gated channel and the pinned Status community is
   decoded from a 5.4MB embedded hex blob then re-fetched from a store node; both `messages.new` and
   `community.found` carry ~1.4MB, parsed 2–3× on the Java side.

UI process
6. `CommunityDto` deep copies in the community service (`asyncCommunitiesDataLoaded`, and every
   `community.found`): 32MB retained for one community on an empty profile. Store once, share by ref,
   and don't retain chats/members for communities the user hasn't joined (the fresh profile only
   *knows about* the Status community).
7. Token catalogue: 13k tokens materialised as Nim `TokenItem`s (+ all-token details fetch) at login.
   Keep the catalogue in status-go and page/filter on demand; keep only tokens of interest in Nim.
8. Fonts: 22 `FontLoader`s read whole files into memory (~10MB with FreeType copies). Register via
   `addApplicationFontFromData` once / drop unused weights / load from a file path so FreeType mmaps.
9. QML units re-compiled at runtime (~4MB + CPU): find which files miss the AOT cache and why.
10. GPU driver host memory (45MB): check texture atlas/size policy and whether the threaded render
    loop's per-frame command allocations are bounded; measure with a `QSG_INFO=1` run.
11. Always-on loaders (communities portal, emoji + sticker popups, dapps service) and sticky sections.

Fixed floor (not fixable by the above)
- ~90MB of mapped code pages per process pair (292MB uncompressed native libs; only touched pages count).
- Qt/QML engine baseline and GPU driver at the login screen.

## Open questions for product/security
- Is lowering the mobile keystore/envelope KDF cost acceptable (memory-hard parameters are the
  point)? If not, the login spike stays and only the release-after-burst lever applies.
- May the app call `FreeOSMemory()` / lower the Go limit briefly after login (CPU/GC cost vs. RSS)?

## Service process: bake-off

See [bakeoff-service-process.md](bakeoff-service-process.md): one-shot `FreeOSMemory` after login −47MB CPU-neutral; lazy zxcvbn and a small enode DB buffer −6MB live each; GC knobs rejected on CPU.

## Service process: ranked offenders

See [service-process-top10.md](service-process-top10.md) — ten items with evidence and remedies, plus the burst timeline and the idle 2.3MB/s allocation sawtooth.

## Code maps

- [ui-process-holders.md](ui-process-holders.md) — what holds memory in the Qt/QML/Nim process.
- [service-process-holders.md](service-process-holders.md) — what holds memory in status-go.

## Next

1. Profile build: Go `MemStats` + heap profile at the settle point (live vs headroom split).
2. heapprofd on the UI process at login screen and at settle.
3. Feature cut-off runs on the profile build (pinned community bootstrap, curated loop, token
   catalogue fetch, emoji/sticker/portal loaders) to size each contributor.
