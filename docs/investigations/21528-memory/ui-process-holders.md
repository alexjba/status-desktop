# UI-process memory holder map (Android) — from code, sizes are estimates

Setup: mobile Nim builds use `--mm:orc -d:useMalloc` (mobile/scripts/buildNimStatusClient.sh:72-73). Nim and Qt share the libc (scudo) heap, so dumpsys "Native Heap" = Qt + QML/V4 + Nim + image decoders + GPU driver.

## 1. Nim services (all init in `load()` after login: app_controller.nim:393-434; pre-login only general/accounts/devices, :381-384)
- **Token catalogue** (biggest): init fires `asyncRefreshTokens(fetchAllTokens=true)` (token/service_main.nim:314) → full `getAllTokens` (~3MB JSON, service_main.nim:254, async_tasks.nim:58-68); kept forever in `allTokensByGroupKey: Table[string, seq[TokenItem]]` (token/service.nim:58, :129). TokenItem ≈ 8 strings + communityData (items/token.nim:26-39). Also tokensOfInterestByKey/groupsOfInterest (service.nim:52-56), price/market/details tables (:67-70); `fetchTokensDetails()` with empty keys fetches details for ALL tokens (Description, AssetWebsiteUrl; service_main.nim:132, dto/token.nim:11-13). Raw `tokenPreferencesJson` string retained (service.nim:72).
- **Communities**: `communities: Table[string, CommunityDto]` (community/service.nim:269); each DTO holds chats, categories, images, members, pending requests, token permissions (dto/community.nim:157-183). Eager `asyncLoadCommunitiesDataTask` at login (:913-924); tags kept as JSON string (:936). Every `community.found` signal inserts a DTO, never evicted (:345-347).
- Chat: chats table + pendingSends (chat/service.nim:142-146), eager getActiveChats. Contacts: table, eager fetch (contacts/service.nim:76, :307). Wallet accounts/keypairs eager (small on fresh). Network/currency/market/stickers/gif: small.
- All Nim section modules load eagerly after login regardless of QML (main/module.nim:915-926): browser, profile, stickers, gifs, activity center, communities, search, wallet, market; chat sections one per community (:1037).

## 2. QML instantiated
- Login screen: onboarding loader + splash (main.qml:610-632), KeycardChannelDrawer loader on Android (:699). `Emoji` singleton already touched (LoginUserSelectorDelegate.qml:76).
- Always active after mainReady: rootChatStore/communitiesStore loaders (AppMain.qml:136-163); **CommunitiesPortalLoader always on, synchronous** (:2354; CommunitiesPortalLoader.qml:15); **StatusEmojiPopup + StatusStickersPopup eager async** (:1647-1660); dAppsServiceLoader (dapps/connector flags default true, :3306-3316) + dappsModuleLoader when third-party on (:3370); contacts adaptors (:249-257); eager stores (:126-185).
- Unloaded on leave: Wallet (:2365), Profile (:2412), Market (:2472), HomePage (:2287).
- Sticky once visited (Binding RestoreNone): Chat (:2308-2320), Browser (:2389-2394), one CommunityChatLoader per community (:2496-2518).

## 3. Images / caches
- No QPixmapCache / QQuickPixmapCache limit set; Qt defaults. No custom image providers except MobileWebView snapshot provider.
- StatusImage sets sourceSize only for .svg (StatusImage.qml:48) → rasters decode at full resolution. `Utils.resizedMediaSource` only for cloudinary URLs (Utils.qml:51).
- HTTP cache: QNetworkDiskCache 512MB disk (constants.nim:25; networkaccessfactory.cpp:92-100).
- **Emoji list**: emojiList.js 1.6MB / 3773 entries materialised in V4 heap AND copied into StatusEmojiModel QJsonArray (Emoji.qml:21-22; statusemojimodel.cpp:41-52). Resident from login screen; guess 5-15MB.
- Resource bundles compiled into libStatus .so (clean pages): resources.qrc 10.2MB, png.qrc 8.7MB, twemoji-svg 8.6MB (3838 files), fonts 5.5MB, img 1.5MB.

## 4. Fonts
- fonts.qrc: 23 files, 5.5MB; 22 loaded via FontLoader in Fonts singleton (StatusQ/Core/Theme/Fonts.qml:12-100). FontLoader on qrc reads whole file into QByteArray → ~5.4MB heap copies for process lifetime.

## 5. Threads
- Taskpool `num_threads = countProcessors()` (threadpool.nim:48) → 8 workers on the Redmi; 2MB virtual stack each; task args serialised to JSON in allocShared (:23-30). Debouncer timers sleep inside pool workers (async_tasks.nim:10-13; debouncer.nim:46-54). Marathon worker thread, Java UiVisibility thread, Qt render loop, QML image + NAM threads.

## 6. QML engine
- Single QQmlApplicationEngine (global_singleton.nim:58-61). `CONFIG += qtquickcompiler`, StatusQ QML bundled into app (Status.pro:5-15, 40-51) → compiled into .so, no disk cache.
- trimComponentCache/clearComponentCache/gc never called; `onLowMemory` no-op (DensityListener.java:58); no onTrimMemory handler.
- Qt modules: webview, widgets, multimedia, svg, core5compat (Status.pro:3,35); `statusq_initializeWebEngine` at boot (nim_status_client.nim:264).

## 7. Signal path (one large signal, e.g. 1.4MB community.found)
1. Binder listener gets Java String (UTF-16, 2x) (StatusGoServiceClient.java:67-100); 2. getBytes(UTF_8) copy (:70,:90); 3. ≥256KB: `getSignalType` parses whole payload as JSONObject just to read "type" (:104-110); 4. nativeDeliverSignal → GetStringUTFChars copy (statusgo_stub.cpp:148-155); 5. Nim cdecl callback → statusq_invoke_method_queued copies to QString UTF-16 in queued event (externc.cpp:281-283), unbounded queue; 6. slot → Nim string, type scan, drop unknown (signals_manager.nim:29-40); 7. parseJson → JsonNode tree (5-10x) → DTO (:45). Raw JSON not retained.
Net: ~10-20MB transient per 1.4MB signal; CommunityDto stays resident. Task results take the same queued QString path (core/tasks/qt.nim:20-26) except finishTyped (:28-42).

## 8. Logging
- Production: no INFO-level JSON dumps (backend/core.nim:34,:56,:43; signals_manager.nim:50 trace). Debug builds (no -d:production) default DEBUG and log full task payloads (threadpool.nim:66-69). User-enabled debug logging also flips production to DEBUG.

## Top suspects by size
1. Token catalogue table + all-token details
2. Community table growth from community.found
3. Transient large-signal copies (incl. Java JSONObject parse)
4. Emoji JS list + QJsonArray copy
5. ~5.4MB font QByteArray copies
6. Full-resolution raster pixmaps (no sourceSize)
7. Sticky Chat/Browser/CommunityChat sections + always-on portal, emoji/sticker popups, DApps service
