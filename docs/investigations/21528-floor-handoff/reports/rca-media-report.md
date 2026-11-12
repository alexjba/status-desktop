# rca-media: issues #2 (no community image in navbar) and #3 (no profile image) on spike/floor-ship — FINAL

Work dir: spikes-floor/rca-media/. Build: triage/apk/shipdbg.apk (app.status.ffloordbg, spike/floor-ship 1e0ae314cb, debuggable release-like).

## Verdict
Not reproduced. Media path works on the ship build: server up, plain HTTP, reachable from the UI process, images render (Lenovo: own avatar + spectated community icon in navbar, also after relaunch). Most likely the owner's fresh test profile simply had no images: a fresh profile with the generated name shows the generic "contact" icon by design, and a community shows in the navbar only once joined/spectated.

## Evidence (Lenovo TB520FU, Android 17, ffloordbg)
- status-go (service process `:statusgo`) log: `media server started {"listeningAddress": "127.0.0.1:38069"}` (pre-login), restarts on login (36999, 34917 after relaunch). Files: dev/ship1/sg-*.log.
- Probe from device shell: `GET /health` → `HTTP/1.0 200 OK`; a TLS ClientHello byte → `HTTP/1.1 400 Bad Request` (= plain HTTP, TLS off as intended). `GET /accountImages?keyUid=<own>&imageName=thumbnail` → `200 image/png`, `Cache-Control: no-store`.
- Fresh profile (dev/ship1/s6.png): navbar avatar = green generic person icon. That is ProfileButton.qml `usesDefaultName → identicon.asset.name "contact"` (ui/imports/shared/controls/ProfileButton.qml:46-55, unchanged by the ship stack except the menu DeferredComponent). No image request is made at all.
- Set a profile picture (Settings → Profile → picker → crop → Save) → navbar + profile page show it (d8.png); after force-stop + relaunch it still shows (d9.png) — served from the media server.
- Communities portal: banners/logos render (d11.png). Opened (spectated) "Meme Dumpster" → its logo appears in the navbar (d12.png) and after relaunch (d14.png).
- logcat (debuggable): no `QML Image: Cannot open` / `Error transferring` for any http://localhost URL. Only image warning: `StatusRoundedImage: Cannot open: qrc:/StatusQ/Components/contact` (pre-existing name-vs-source quirk, unrelated).

## Code path (ship)
- Nim asks status-go for HTTP on Android: src/backend/accounts.nim:299-301 (`mediaServerEnableTLS=false` in InitializeApplication); status-go honours it (mobile/status.go:102 SetMediaServerOptions → pkg/backend/node/get_status_node.go:200 → media.WithMediaServerDisableTLS); URL scheme comes from the server itself (internal/httpserver/server.go:427 https only with a cert), so status-go-built URLs (identity images, community images) are http:// automatically.
- Nim-built contact URLs: src/app_service/common/media_server_url.nim MEDIA_SERVER_BASE = http on Android (matches).
- QML fetch: engine QNAM (ui/StatusQ/src/networkaccessfactory.cpp) — https GETs go to PlatformHttps (Java HttpURLConnection); http (loopback media) stays on Qt's native socket. Android cleartext policy (no usesCleartextTraffic / networkSecurityConfig in the manifest, targetSdk 36 → cleartext "blocked") applies only to Java/Conscrypt/WebView stacks, not to Qt's native sockets, so loopback http is unaffected. If someone later routes http through PlatformHttps too, it WOULD hit the cleartext block — worth a comment.

## Redmi A5 (Android 15 / HyperOS), same ffloordbg build
- Fresh profile: media server up (`127.0.0.1:41655`, later 38165), `GET /health` from device shell → 200 (dev/redmi1/probe.txt). Wallet remote https token logos render (PlatformHttps path).
- Drawer (phone navbar) on a fresh profile: generic contact avatar, no community entries (dev/redmi3/d1.png) — same as the owner's report, and expected: nothing to show.
- Spectate-a-community + drawer on the Redmi: not run (job cancelled on lead request). Lenovo covers this path.

## Master comparison
Not run (fbase build failed on ship-triage's side). Not needed for the verdict: the fresh-profile avatar logic (ProfileButton `usesDefaultName → "contact"`) and the "community in navbar only when joined/spectated" behaviour are identical on master; master would show the same thing for a fresh profile.

## What to ask the owner (decides whether anything is left)
1. Had the test profile a profile picture set (Settings → Profile → picture) before the avatar went missing? If not, the generic icon is by design.
2. Was a community joined/spectated? If yes, did it show as a letter/colour circle in the drawer, or not at all?
If (1) or (2) is "yes, and it was blank": re-test on app.status.ffloor-equivalent release (R8 + order files) — the only differences from ffloordbg (debuggable flag, R8 Java shrink, hot-first order files) don't touch Qt's http path, so an unknown cause would be needed.

## Fix options (only relevant if the owner confirms a real missing image)
| option | cost | risk | memory |
|---|---|---|---|
| A. Nothing to fix; close #2/#3 as "fresh profile, by design" | 0 | none | 0 |
| B. Diagnostic: log failed QML image loads for loopback URLs (Status::HttpStats already records host:port per reply; add an error counter / warning for `localhost:` failures, visible in release via HttpStats screen) | ~1 h | none | ~0 |
| C. Fallback: if the media server URL fails (refused / scheme mismatch), re-request with the other scheme (https↔http) or ask status-go for the current port via `mediaserver.started` (already handled by withMediaServerPort) | ~0.5 day | low; masks real config bugs | 0 |
| D. Revert a8f4baa36f (TLS back on loopback) | 10 min | brings OpenSSL back: login −10.8 / post −8.6 MB saving lost (ssl-report) | +8..11 MB |
Recommendation: A, plus a code comment at networkaccessfactory.cpp that plain-http loopback must stay on Qt's socket (routing it through PlatformHttps would hit Android's default cleartext block, targetSdk 36, no network_security_config).

## Side observations (not in scope, for other agents)
- debuggable logcat: many `QObject: Cannot create children for a parent that is in a different thread` (parents = QML items: StatusScrollBar, StatusMessage, StatusSwitch...; current thread ≠ GUI thread) during onboarding/login — something creates QObjects parented to QML items off the GUI thread (async incubation/DeferredComponent/QmlCompiler worker?). Possible relevance to the crash (#8). dev/ship1/lc.clean.txt.
- `TypeError: Cannot read property 'dappsModel' of null` once at startup.
- Lenovo relaunch: still black at 35 s after launch, UI at ~50 s (d13/d14.png) — stay-logged-in path, debuggable build; not measured further.
