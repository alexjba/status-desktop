# Balance-poll churn on an idle, empty wallet

Context: the service process allocates ~105MB per balance fetch, and a fetch runs every 2 minutes
even on a fresh profile with one address and zero balances. The earlier "2.3MB/s idle" figure was a
60s window that caught one fetch; the true idle rate between fetches is ~0.4MB/s.

## Triggers (pkg/services/wallet/multistandardbalance/controller.go)
- 2-min ticker (:356-366) → `TriggerFullFetch` → 10s trailing debounce (:130, :395). **Every tick is a
  full fetch**: `pendingFullFetch` bypasses the staleness check in `computeBalancesToFetch` (:452).
- Start: immediate full fetch (:370); storage is in-memory (service.go:241) so every process start is cold.
- Token-lists-updated (:332-336, from token.go:303): at most every 30 min; on a fresh start the first
  refresh re-arms the leading edge, giving a second immediate full fetch.
- Not idle: AccountsAdded, ActiveNetworksChanged, pending tx deleted, tx success (no debounce, :601),
  `RestartWalletReloadTimer` from the UI reload/retry paths, and every Resume after Pause (service.go:531-570).
- No new-block watcher; the reader is a consumer only.
- Hazards: `c.stopCh` read without a lock and nil'd after close (:155-159 vs :236/:260/:362/:571) — a
  goroutine not parked in select at Stop never exits (same pattern in transferdetector); the debounce is
  not cancelled on Stop, so a fetch can run up to 10s after Pause.

## Per-run work
- Scope: accounts × active mainnet chains (1, 10, 42161, 8453, 56) × **the full catalogue**
  (`multistandardbalance_adaptors.go:22-34` → `token.go:434`), ~11.7k embedded tokens, ~17k after the
  remote list refresh. Tokens of interest are not used. Also one `GetCustoms` DB query per account × chain.
- One job per account per type (sdk multistandardfetcher/fetcher.go:71-113); each `balanceOf` goes through
  `GetAbi` + `abi.Pack` (sdk multicall/erc20.go:15-20); chunks of 2500 (`DefaultBatchSize`), first chunk
  `tryBlockAndAggregate`, rest `tryAggregate` pinned to the block.
- Encode per 2500-call chunk (benchmarked): build calls 0.64MB / 17.5k allocs; `abi.Pack` of the whole
  `(bool,(address,bytes)[])` 7.13MB / 61k allocs (calldata 480KB; includes `mapArgNamesToStructFields`,
  `ToCamelCase` per tuple); `json.Marshal` of the request 3.48MB (body 960KB) — marshalled twice
  (rpc/client.go:547 then http.go:205), ~3.6× body size; puzzleauth `readRequestBody` does `io.ReadAll`
  on the body (puzzleauth/transport.go:67-92, :97) although geth already sets `GetBody` (http.go:211).
- Decode: `json.Decoder` refill → RawMessage → hexutil.Bytes → reflective `abi.Unpack` → `ConvertType`
  copy into `[]IMulticall3Result` → one `big.Int` per call including zeros (erc20.go:35) →
  `processERC20JobResult` builds a map of every token including zeros (fetcher_erc20.go:27-51), kept by
  storage_memory.
- RPC: `ClientWithFallback.makeCall` adds a goroutine, ctx and hystrix command per call; failures walk
  providers re-marshalling each time; sdk `executeChunkWithRetry` halves failing chunks (3× pack cost).

## Other loops on a fresh profile (not significant in the window)
transferdetector 10 min; activityfetcher 10 min; collectibles PeriodicalLoader 60 min; currency 1h;
token lists check 3 min / refresh 30 min; leaderboard only while subscribed; pendingtxtracker only with
pending txs; rpclimiter 1s ticker per provider (tiny).

## Ranked allocation cuts (per fetch ≈ 105MB ≈ 50MB/min at the 2-min cadence)
1. **Ticks fetch tokens of interest only** (cached non-zero balances, mandatory, custom/community, tokens
   seen by transferdetector — cf. reader.go:196); keep the full sweep for cold start, token-list update,
   user refresh, or every 30–60 min. Saves ~100MB/fetch and ~17k RPC calls → <50 on an empty profile.
   Risk medium: a token received without a detected Transfer shows up late.
2. Hand-encode `tryBlockAndAggregate`/`tryAggregate` + `balanceOf` (selector `0x70a08231` + padded
   address) into one preallocated buffer, bypassing `abi.Pack`; optionally cache per chain/account/list
   version. 7.13MB → ~0.5MB per chunk, ~45MB/fetch. Risk low (byte-identical output testable).
3. puzzleauth: use `req.GetBody()` per attempt instead of `io.ReadAll`. ~17MB/fetch and the same on every
   other RPC call. Risk very low.
4. Hand-decode results; skip Unpack/ConvertType/big.Int for zero balances; store only non-zero.
   ~12MB/fetch + 1–2MB retained. Risk low (readers must treat missing as zero).
5. Raw `eth_call` with a pre-hexed body to drop the double JSON marshal; provider fallback reuses bytes.
   ~20MB/fetch. Risk medium (RPC layer).
Bonus: cache `GetTokenContractAddresses` per chain per list build (~3.6MB/fetch, trivial).

Cut 1 removes nearly all churn on an empty profile and most of the network/battery cost of the
sweep; cuts 2–5 keep paying on real wallets where the sweep remains.
