# Service-process bake-off (Redmi A5, profile build on madvise + hoist baseline)

Each variant is one change (or one combination) on top of the baseline build, measured with
`mobile/scripts/profiling/measure_run.sh` (relaunch → login → 2 min settle → forced-GC live heap,
30s CPU profile, CPU seconds from `/proc/<pid>/stat`). Raw rows: `ledger/bakeoff-service-process.csv`.
Single runs vary by ±25MB PSS (GC sawtooth phase + scavenger lag), so rank on the forced-GC live
floor and on repeated passes; PSS is reported as the median.

| variant | runs | PSS med | live after GC | native | login CPU* | idle CPU/120s | verdict |
|---|---|---|---|---|---|---|---|
| baseline (status-go#7864 + #7865) | 5 | 213 | ~40 | 16 | 24s | 5.3s | — |
| E5 one-shot `debug.FreeOSMemory()` 45s after login | 2 | 166 | 38 | 6 | 30s | 5.9s | ship: −47MB, CPU-neutral |
| no-GC combo (E5 + E2b + E3 + metrics off) | 2 | 168 | 38 | 7 | 30s | 5.3s | native −9MB on top of E5 |
| lazy zxcvbn dictionaries (fork: drop `init()`) | 2 | 208 | 34 | 8 | 24s | 5.1s | ship: −6MB live |
| enode in-memory DB 64KB write buffer (go-ethereum) | 2 | 218 | 34 | 14 | 45s | 5.0s | ship: −6MB live |
| E3 sqlcipher 3 conns + 1MB cache | 1 | 201 | — | 15 | — | — | native −3 |
| E4 geth metrics off (+ zxcvbn stub) | 1 | 209 | — | 12 | — | — | native −6 |
| E2b coingecko ids resolved per key | 1 | 207 | — | 10 | — | — | churn cut; memory within noise |
| E1 GOGC=50 + 256MB limit | 1 | 172 | — | 11 | — | — | reject (see E1a/E1b) |
| E1a GOGC=50 | 3 | 214 | 39 | 16 | 46s | 7.9/5.2s | reject: GC share 0.41→1.04s per 30s |
| E1b limit 256MB | 3 | 215 | 40 | 17 | 30s | 6.0s | reject: no gain, GC thrash in the burst |
| E2 coingecko full-map cache | 1 | 222 | — | 11 | — | — | reject: retains what it saved |

\* login CPU (Go process, 20s window after Enter) includes the RPC-proxy argon2 puzzle whose nonce
count is random (16–57s across identical builds); only E1a's doubling is a clear signal.

GC share of the 30s idle CPU profiles: base 0.41s, GOGC=50 1.04s, limit-only 0.40s, combined 0.72s.

Projection: E5 + metrics off + sqlcipher caps + lazy zxcvbn + enode buffer ≈ 145–150MB with no
CPU-costing change. The balance-sweep scope fix (balance-poll-churn.md, cut 1) then removes
~105MB of allocations and ~17k RPC calls per 2-minute tick.

Experiment branches live in the worktree submodule: `exp/free-os-memory`, `exp/combined-nogc`,
`exp/zxcvbn-lazy` (go.mod replace → scratchpad fork), `exp/enode-smallbuf` (replace → patched
go-ethereum), `exp/sqlite-pool`, `exp/static-tables`, `exp/coingecko-resolve`, `exp/gc-policy`,
`exp/e1a`, `exp/e1b`, `exp/combined`.
