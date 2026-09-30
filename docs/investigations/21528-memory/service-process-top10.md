# Service process (`:statusgo`): top memory offenders and what to do about them

Measured on the profile build with #7864 (MADV_DONTNEED) and #7865 (mandatory-token hoist),
fresh profile, Redmi A5. Settle-point PSS of the service process is now **220–270MB**
(`ledger/profile-build-redmi-a5.csv`, rows `hoist2-*`, `burst-end`), composed of:

| bucket | MB | notes |
|---|---|---|
| Go heap resident (`HeapSys − HeapReleased`) | 100–130 | live floor ~42MB + garbage up to the GC goal (sawtooth to ~110MB) + not-yet-scavenged idle |
| mapped code/rodata of libstatus.so (`.apk mmap`) | 63–74 | only touched pages count; see #8 |
| native malloc (sqlcipher, OpenSSL, cgo) | ~20 | see #7 |
| Go runtime metadata (GCSys, MSpan, stacks, BuckHash, Other) | ~12 | 282 goroutines, 3MB stacks |
| Dalvik + Java side of the service | 3–8 | transient signal copies |
| libsds (Nim refc heap) and other anonymous | ~10–20 | by subtraction; not directly measured |

Burst timeline after login (`burst/heapdbg-*.txt`): HeapSys reaches ~504MB before T+6s
(scrypt + argon2 + bootstrap decode), live drops to ~70MB by T+12s, then an idle sawtooth of
**~2.3MB/s allocation** between ~42MB and ~110MB with a GC every ~20s.

## Ranked

### 1. Startup burst: KDFs (scrypt 256MB, argon2 puzzle 64MB × attempts) — peak, not steady state
- Evidence: `allocs` cumulative: scrypt.Key 260MB per login (572MB at profile creation), argon2
  512–880MB per session (`puzzleauth/solver.go:58`, memory chosen by the RPC-proxy server, a
  fresh block set per nonce attempt).
- Effect: sets HeapSys high-water (~500–690MB). After #7864 it leaves RSS within seconds; before
  it, it was the whole story.
- Do: (a) keep scrypt as is unless product accepts a KDF downgrade (see README); (b) argon2: reuse
  the block memory across nonce attempts (x/crypto allocates per call → fork or wrap with a
  pooled implementation), or negotiate a lower `MemoryKB` for mobile with the proxy; (c) run the
  puzzle solve *after* the login KDF, never concurrently, so the two 256MB-class allocations don't
  stack. Also lower the Go memory limit during the login window so the GC reclaims KDF garbage
  before the next allocation.

### 2. GC headroom policy — ~40–65MB at settle
- Evidence: GOGC=100 (no SetGCPercent), heap goal ≈ 2× live; live floor 42MB, heap climbs to
  ~110MB before each GC. Memory limit 500MB only matters near 500MB.
- Do: on mobile set `debug.SetGCPercent(50)` (peak ≈ 1.5× live, ~65MB) and lower
  `SetMemoryLimit` to ~256MB now that MADV_DONTNEED makes the limit-driven scavenger effective.
  Cost: GC runs ~2× as often on a churn that #3 will shrink anyway. Alternatively keep GOGC and
  cut churn (#3) so the periodic forced GC bounds the sawtooth.

### 3. Idle allocation churn: 2.3MB/s on an empty wallet — feeds #2, costs CPU/battery
- Evidence: 60s idle diff (`allocs-idle-A/B`): `multicall.ProcessJobs` 51MB (abi.Pack 45MB),
  `json.Marshal` 26MB, `io.ReadAll` 17MB, JSON decode 9MB, `multistandardfetcher` jobs.
  Balance controller: `FetchPeriod` 2min, `FetchDebounceTime` 10s
  (`wallet/multistandardbalance/controller.go:68`), but multicall packing is running far more often
  than every 2 minutes on a profile with one address and zero balances.
- Do: find the extra triggers (block/chain events, activity fetcher, collectibles
  `PeriodicalLoader`) and coalesce; cache packed calldata per (chain, token set) instead of
  `abi.Pack` per poll; skip balance polling entirely for addresses with no history until an
  inbound transfer is seen; stop `io.ReadAll` of RPC bodies into fresh buffers (stream decode).

### 4. coingecko coin list retained in the heap — ~15MB live
- Evidence: heap-35 cum: `coingecko.(*Client).getCoingeckoToken*` 14.7MB (JSON-decoded
  `/coins/list`, thousands of entries, kept as `map[string]GeckoToken`;
  `request_coins_list.go:51`, `client.go:93`). Also 0.46GB cumulative churn re-fetching/decoding.
- Do: persist the id mapping in the wallet DB (it changes rarely), keep only entries for tokens
  of interest in memory, and stop re-decoding the full list per refresh. On a fresh profile this
  alone is a third of the live floor.

### 5. Token catalogue rebuilds — 4–6MB live + 10–20MB transient per rebuild + 3.3MB rodata
- Evidence: `StandardTokenListParser.Parse` 4MB live via `Builder.AddRawTokenList`; embedded
  default lists ~2MB in init; 13,378 tokens in 8 lists; `buildState` re-parses every list on each
  rebuild (`token.go:99-155`, go-wallet-sdk manager.go:487-521); Nim then asks for the whole
  catalogue twice (`getAllTokens`, `getAllTokenLists`, ~3MB JSON each).
- Do: rebuild incrementally (parse only lists whose blob changed), serve the catalogue to the UI
  paged/filtered instead of two 3MB payloads, and keep the embedded lists compressed on disk rather
  than as Go rodata (they are also part of #8).

### 6. Pinned Status community bootstrap + community JSON — transient 20MB + 2 × 1.4MB signals
- Evidence: 5.4MB hex blob read → string → decode → protobuf copies
  (`pinnedcommunities/loader.go:61-79`); `Community.MarshalJSON` repeats the member map per
  token-gated channel; `messages.new` and `community.found` each ~1.4MB, parsed 2–3× in Java.
- Do: ship the blob as raw bytes (not hex, halves it) or compressed; decode straight from the
  embedded slice; don't publish the full community in `messages.new` when the store-node fetch
  will emit `community.found` seconds later; drop the per-channel member repetition from the JSON.

### 7. sqlcipher connection pools — ~20MB native
- Evidence: `MaxOpenConns = MaxIdleConns = min(GOMAXPROCS, NumCPU)` = 8 per DB × 2 encrypted DBs
  (`internal/db/sqlite/sqlite.go:236-245`); default page cache per connection; native heap of the
  process ~20MB (heapprofd: 7.6MB attributed to sqlcipher/`sqlite3_open_v2` paths).
- Do: on mobile cap at 2–4 connections per DB and set `PRAGMA cache_size` explicitly (e.g. -1024);
  idle connections could also be closed after a timeout (`SetConnMaxIdleTime`).

### 8. Binary size: 63–74MB of touched libstatus.so pages
- Evidence: `llvm-size`: .text 25.7MB, .gopclntab 22.8MB, .rodata 14.8MB, .data.rel.ro 9.2MB,
  .rela.dyn 8.6MB (relocations dirty their pages at load), .noptrbss 32MB
  (`crypto/internal/fips140/drbg.memory`, untouched unless used). Big rodata symbols: pclntab 20MB,
  strings 10.9MB, type metadata 7.9MB, embedded token lists 3.3MB, secp256k1 tables 1MB, gomoji.
- Do: this is the floor's largest piece and moves only with dependency weight. Candidates: drop
  go-ethereum surfaces not used on mobile (p2p/enode, discv5, metrics), libp2p transports not used
  by a light client (QUIC/WebTransport), hystrix (23 monitor goroutines for circuit metrics),
  `gofakeit` (pulled into prod via `Community.Fake`/`Contact.Fake`), blackfriday. Each removal
  shrinks pclntab/types/strings proportionally. Measure with `llvm-size` per PR.

### 9. Static tables initialised at startup — ~10MB live
- Evidence (heap-35 flat): zxcvbn frequency dictionaries 3.1MB (`mobile/status.go`
  `GetPasswordStrength`, built at init via `loadFrequencyList`), enode in-memory leveldb 4MB
  (`go-ethereum/p2p/enode.newMemoryDB` — created by the waku node even as a light client with
  discv5 off), prometheus label pairs 1.5MB + geth `ExpDecaySample` heaps 1MB (metrics enabled by
  `-X ...metrics.EnabledStr=true` in the Android ldflags), secp256k1 precomputed 1.1MB, blackfriday
  1MB, gomoji 0.6MB, protobuf descriptors 1MB, `gofakeit` data 0.5MB, emojihash alphabet 0.5MB.
- Do: lazy-load zxcvbn on first password check and drop it afterwards (or move the check to the
  client); don't open the enode DB on light clients; drop `metrics.EnabledStr=true` for mobile unless
  rpcstats needs it; keep the crypto tables.

### 10. Goroutines and their timers — 282 goroutines, 3MB of stacks, steady wakeups
- Evidence (`burst/goroutines.txt`): hystrix pool/metric monitors 46, libp2p swarm/yamux ~44,
  waku `processQueueLoop` 8, RPC limiters 6, health managers 5, collectibles `PeriodicalLoader` 4,
  QUIC reuse gc 4, filter multiplexers 6, encryption publisher ticker.
- Do: memory is small; the value is CPU/wakeups. Replace hystrix with a lighter breaker (its
  monitors tick per command), and gate periodic loaders on "has something to load".

## Not on the list on purpose
- libsds: ~10–20MB by subtraction, refc heap; revisit only if the above lands and it remains visible.
- Memory limit value: irrelevant until the burst is reshaped (#1) — with MADV_DONTNEED it's an
  OOM guard, not a footprint lever.

## Suggested order
#2 (GC policy: one-line, measurable), #4 (coin list), #3 (churn), #9 (static tables, several
one-liners), #7 (sqlcipher pools), #5, #6, #1(b/c), #8, #10.
