# Service-process (status-go 3ea3c7f424) memory holder map — from code, sizes are estimates

## 1. GC / runtime
- `pkg/backend/geth_backend.go:170-176`: `SetMemoryLimit(500MB)` only on mobile; no SetGCPercent, no GOGC/GOMEMLIMIT env anywhere (incl. Android Java/JNI). GOGC=100 → heap goal ≈ 2× live until the 500MB limit.
- `internal/platform/platform.go:11-13`: IsMobilePlatform = GOOS android/ios; Makefile:65 builds with GOOS=android → true.
- FreeOSMemory never called in the library (only cmd/status-backend/server/server.go:260).

## 2. Waku (light/Edge client on mobile)
- `accounts/service.nim:204` sends wakuV2LightClient=IS_MOBILE → `pkg/backend/defaults.go:330,:134` → `pkg/services/ext/service.go:194` → `pkg/messaging/waku/mode.go:24`. Edge mode (mode.go:51-56): no discv5, no PX server, PX client on.
- `gowaku.go:325-330`: filter light node, max peer connections = DiscoveryLimit 20 (config.go:81, defaults.go:130). No relay/filter-full/lightpush-server/store-server. `gowaku.go:303` max msg size 1MB.
- libp2p defaults (go-waku v0.10.3 wakuoptions.go:630-643): TCP+QUIC+WebTransport, yamux, NAT, hole punching, connmgr 200/300, autoscaled resource manager. ~20 conns × 100-300KB ≈ 2-6MB.
- In-memory: envelope-hash ttlcache 20min TTL, no cap (gowaku.go:92,146,220) — hashes only; msg queue chan 1024 (:88,:256); send queue 1000 (:271). transport.go:55 received map; envelopes_monitor.go:88-91; filters_manager.go:23-34. All small on fresh profile. Dedup is DB-persisted.
- nim-sds ReliabilityManager in-process (pkg/messaging/layers/reliability/reliability.go:83) — Nim heap, anon but not Go heap.

## 3. SQLite / SQLCipher
- Open after login: `<keyUID>-v4.db` (app), `<keyUID>-wallet.db`, unencrypted multiaccounts DB (1 conn; multiaccounts/database.go:123, sqlite.go:270). App+wallet opened in parallel (geth_backend.go:512-530).
- `sqlite.go:236-245`: MaxOpen=MaxIdle=min(GOMAXPROCS,NumCPU) = 8 on the Redmi → up to 16 encrypted connections, idle ones stay open. cipher_page_size 8192 (:29,:191). No cache_size/mmap_size pragma → default ~2MB page cache per conn (lazy, bounded by DB size); schema parse ~0.3-1MB per conn. Estimate 10-40MB worst case, native malloc not Go heap; fresh profile likely <15MB.

## 4. Tokens / wallet
- 19MB Go source `pkg/services/wallet/token/local-token-lists/default-lists/*.go`: 8 embedded lists, 3.3MB JSON, 13,378 tokens (Ethereum 4765/1.04MB, BSC 3270/717KB, Base 2296/508KB, Uniswap 1453/613KB, Arbitrum 873/197KB, Status 380/146KB, Optimism 252/55KB, Linea 89/20KB). Lives in .so data → .apk mmap, not anon.
- NewTokenManager (token.go:99-155) on go-wallet-sdk manager; `buildState` (go-wallet-sdk manager.go:487-521) builds a fresh Builder each time: map of unique tokens + map of lists sharing *Token (builder.go:33-34,118-129). Each rebuild json.Unmarshals every list (embedded or DB blob via contentStore.GetAll, tokenlists_adapter.go:60).
- AutoRefreshTokensEnabled default true (defaults.go:108): fetches prod.market.status.im/static/lists.json + each list (token.go:51), stores blobs, rebuilds. Retained ≈ 13k × 300-400B ≈ 4-6MB Go heap; 10-20MB transient per rebuild.
- Nim calls getAllTokens + getAllTokenLists at startup (token/async_tasks.nim:62,:97). GetAllTokens/GetAllTokenLists (token.go:666-739) allocate a wrapper per token; responses ~3MB and ~3.5MB, each copied marshal→string→C.CString→JNI NewStringUTF (statusgo_service_jni.cpp:146)→Java.
- Market caches keyed by tokens of interest (market/market.go:55-76), small. Leaderboard polls 10s/1s only while UI subscribed (leaderboard/config.go:12-13; service.go:333). Fetcher HTTP client 10 idle conns (fetcher/httpclient.go:15).

## 5. Communities (source of the ~1.4MB community.found)
- Pinned-community bootstrap on fresh profile only when community count is 0 (`internal/protocol/messenger_pinned_communities.go:20-138`): loads embedded 5.4MB hex `pinnedcommunities/assets/0x02b5bd…rawpayload.hex` (Status community). Loader (pinnedcommunities/loader.go:61-79): 5.4MB ReadFile + 5.4MB string + 2.7MB hex decode + protobuf copies ≈ 20MB transient. Publishes MessengerResponse (line 115 → messages.new signal) and FetchCommunity from store node (line 101).
- Fetch completion: messenger_store_node_request_manager.go:338-340 → passStoredCommunityInfoToSignalHandler (messenger_communities.go:3134) → community.found (pkg/services/ext/signal.go:52).
- Curated loop at login then hourly (messenger_curated_communities.go:25-89): reads Optimism directory, store-fetches every unknown curated community (:137-165), each emits community.found.
- Payload size: Community.MarshalJSON (communities/community.go:~300-420) includes full members map (enrichMembersMap) AND the member map again per token-gated channel (chat.Members), plus pendingAndBannedMembers, tokenPermissions. (Inferred from code, member count not decoded.)
- Caches: Readonly ttlcache cap 5 / 1min TTL (communities/manager.go:324). Media-server image/token readers (manager.go:398-411) use uncached GetByIDString → full DB load + unmarshal of multi-MB description per community image HTTP request.
- Torrent client not built (Makefile:179-181), TorrentConfig.Enabled false (defaults.go:357-362). IPFS downloader chan 1000 (ipfs.go:73), small.

## 6. Other services at login (pkg/backend/node/status_node_services.go:76-135)
- Unconditional: rpcstats, networks, appgeneral, personal, pendingTracker, ens, communityTokens, stickers, updates, gif, chat, eth, wallet (when enabled), wakuext, local notifications, sharedUrls, linkPreview, accounts/preferences/storageStats.
- Newsfeed only when flag + third-party on (:484). Browsers/permissions/connector config-gated.
- Wallet workers (wallet/service.go:531-548): multistandardBalance, transferDetector, currency (hourly), signals, collectibles, leaderboard, activityFetcher, reader — skipped when third-party disabled (:505).
- Messenger.Start (internal/protocol/messenger.go:608+): pushNotificationClient, ensVerifier, communitiesManager, historic sync worker, history reconciliation, cursor monitor, settings loops, rekey, curated loop, missing-HR-keys loop, pinned bootstrap goroutine, history-archives import loop, httpServer.
- No large in-memory caches found on fresh profile; media server has no cache.

## 7. Signal path (Go → Java → UI)
- `internal/signal/signals.go:49-66`: json.Marshal (copy 1, fills encodeState pool) → string (2) → C.CString (3, freed after callback). JNI (statusgo_service_jni.cpp:35-45) NewStringUTF (4).
- `StatusGoService.java`: onNativeSignal (184-197) getBytes for size (5); maybeStartForegroundFromSignal (:304) full JSONObject parse of EVERY signal; StatusNotificationManager.handleSignal (:88-91) second full parse (returns after parsing when UI visible); ≥256KB getSignalType third parse (:295); dispatchSignalToListeners (203-241) getBytes again (6); ≥128KB (:52) getBytes third time (7) + one SharedMemory per listener (:219).
- 1.4MB community.found ≈ 7 linear copies (~10MB) + 2-3 org.json trees (5-10× each, Java heap). Transient. No signal queue when UI unbound — dropped.

## 8. Logging
- Nim sends DEFAULT_LOG_LEVEL (env_cli_vars.nim:12): INFO with -d:production, DEBUG otherwise; buildNimStatusClient.sh:88-95 omits -d:production for DEBUG=1 and PROFILE builds → status-go + go-waku (gowaku.go:301) at DEBUG in those builds. Default without request ERROR (defaults.go:32).
- RPC/signal bodies logged only with api logging on (mobile/status.go:188-193), off by default (env_cli_vars.nim:314-318). lumberjack rotation (logrotation.go:37), MaxBackups 10, 100MB default; no in-memory buffers.

## Top suspects
1. GC headroom (GOGC 100 under a 500MB soft limit)
2. Startup churn: token lists parse/rebuild (3.3MB JSON) + pinned Status community decode (5.4MB hex)
3. Big community JSON (member map repeated per gated channel), 2 signals, parsed 2-3× on Java side
4. Debug-build log level
