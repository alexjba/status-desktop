# webproc spike — adversarial review

Scope: mobilewebview `97455aa..a4e0050` (+ `df2843a` separately), status-app `5600a419bd..4cb40e0e56`.
Paths below are relative to `mobilewebview/android/src/org/mobilewebview/` unless prefixed.
Read-only review; nothing built or run. "Verify" = I could not confirm from code, needs a device check.

---

## BLOCKERS

### B1. Crop-not-relayout permanently hides the top of the page (owner symptom "can't scroll back to top")
`RemoteWebView.java:505-547`. `mFull` only resets to the item rect when the host size changes (rotation) or on first layout. Otherwise it **only grows**: one geometry tick with `item.top < full.top` (navbar sliding away, or any item move up) makes `full.top = host.top` (0, i.e. under the status bar / top chrome when host content is edge-to-edge), and it never shrinks back.
- Scenario: open navbar → close it → WebView now laid out from host y=0; the panel crops at item.top. The page's scroll origin (scrollY=0) sits under the crop → the top `item.top` px are unreachable forever (until rotation). With the navbar open, the top `navbarHeight` px are unreachable by design.
- Second-order: `onLinkLongPressed(x,y)` (`WebViewCore.java:1040`) and freeze snapshots are in full-rect coords, the QML item is smaller → context menu offset by the crop, freeze image stretched/misaligned when a popup opens with navbar shown.
- Inherent to "crop instead of relayout" (Chromium WebView has no "top controls" inset API like Chrome). Fix: relayout once settled (ca11577 did this; accepts one reflow), or Chrome-style scroll-linked navbar (needs scroll-offset IPC). At minimum: shrink `mFull` back to the item rect after the animation settles, never grow to host edge.

### B2. Leaving the page for >5 s reloads it and drops in-flight dApp state; signed responses can be lost
`BrowserProcessClient.java:28` (`IDLE_KILL_MS = 5_000`), `RemoteWebView.java:167-169` (`keepsProcess = mVisible || mFrozen`), `RemoteWebView.java:569-586` (freeze = "hidden within 2 s of a capture", `FREEZE_HIDE_WINDOW_MS`).
- Any hide that is not recognised as a freeze (a QML overlay that hides without capture, capture failing, bookmarks/tabs/history list kept open, switching to Wallet tab to check a balance, app switch if QML hides the item) → after 5 s `saveAll` + unbind → Chromium dies. Return = cold start + network reload: scroll position, form input, SPA state, WalletConnect/dApp in-memory session gone.
- dApp request in flight: dApp sends `eth_sendTransaction` → Status signing modal (if it hides rather than freezes, or user takes >5 s on another screen) → page process killed → user signs → tx broadcast → response goes through `postMessageToJavaScript` → `call()` queues it (`BrowserProcessClient.java:200-209`) → replay recreates the WebView, restores history, then executes the queued deliver script on a page that is still loading/never registered the request → **lost**. dApp shows "pending"/error, user retries → duplicate tx. Same for `evaluateJavaScript` results (no correlation id; `mobilewebviewbackend_android.cpp:1320`).
- Downloads die too: self-fetch downloads run in `:browser` (`WebViewCore.java:757-` `mDownloads.*`), `destroy()` calls `mDownloads.cancelAll()`; `keepsProcess()` ignores active downloads → leaving the Browser cancels a download within 5 s.
- Fixable: TTL tied to "left Browser section" / `onTrimMemory` instead of visibility; keep process while downloads or dApp requests are pending; drop queued bridge responses (don't replay them into a fresh page) and fail them back to the dApp layer explicitly; move downloads to the UI process (they need cookies only — fetch them over IPC).

### B3. No `onRenderProcessGone` → renderer crash/LMK becomes a `:browser` crash loop
`MobileWebChromeClient.java`/`MobileWebViewClient.java`: no `onRenderProcessGone` anywhere (grep). Default WebView behaviour = kill the embedding process.
- Before: renderer death killed the whole app. Now it kills `:browser`, `onHostGone` (`BrowserProcessClient.java:228-246`) → `updateIdle` → rebind immediately → replay restores the **same URL** → a deterministic renderer crash (heavy WebGL dApp, OOM page) loops forever: spawn → load → crash, burning CPU/data/battery, UI shows a flickering blank page, no error, no backoff.
- Same loop under memory pressure: LMK kills `:browser` (largest same-adj process) → immediate respawn, also while the app is backgrounded (no foreground check before rebinding).
- Fixable: implement `onRenderProcessGone` (return true, destroy WebView, report "page crashed" to QML); crash counter + exponential backoff; only respawn when the Browser is visible and the app is foreground.

### B4. Focus/IME split across three windows in two processes — root of the IME and Back symptoms
Windows: Qt activity window, UI-process panel (`WebViewWindow`, `TYPE_APPLICATION_PANEL`), embedded SCVH window owned by `:browser`. Key/IME focus belongs to exactly one, and every hand-over is async (WM relayout + Binder).
- Page touches never reach the panel: the SurfaceView is `setZOrderOnTop(true)` (`RemoteWebView.java:98`) and covers the panel, so `Root.dispatchTouchEvent` never sees DOWN on the page. Focus is granted only when `onTouchDown` comes back over Binder (`RemoteWebView.java:259-264` → `WebViewWindow.contentTouched` → `updateViewLayout` → `SurfaceView.requestFocus` → embedded focus grant). Chromium asks for the IME on tap-up → the request lands before the grant → dropped ("first tap doesn't open keyboard"; what df2843a tries to patch).
- **"IME hide acts as Back / sometimes does nothing"**: with `enableOnBackInvokedCallback="false"` (status-app `AndroidManifest.xml:53`) the IME-hide button is a BACK key delivered to the **focused window**, and only that window's IME session can consume it. If the IME serves the Qt window but focus is on the panel/embedded window, the IME doesn't consume it:
  - panel focused, SurfaceView not view-focused → `Root.dispatchKeyEvent` → `onBack` → `mOwner.onBackRequested` (`WebViewWindow.java:303-308`, `RemoteWebView.java:120-127`) → QML navigates back while the IME also hides (focus moved) = "hides + navigates back".
  - panel focused and `mContent.hasFocus()` true but embedded grant not done → `super.dispatchKeyEvent` → SurfaceView ignores it → dropped = "does nothing".
  - embedded window focused → `:browser` `WebView.dispatchKeyEvent` forwards Back unconditionally (`WebViewCore.java:153-163`).
- **Address bar "keyboard doesn't come back"**: likely the same race. Tap on the Qt field → panel gets `ACTION_OUTSIDE` → `setFocusable(false)` → async relayout; Qt calls `showSoftInput` on that same tap, before the activity window has focus again → IMM ignores it (not the served/focused window). The first tap can win the race. A second tap on an already-focused field gets no retry. Verify with `adb logcat -s InputMethodManager` ("Ignoring showSoftInput… not served"/window not focused).
- Hung `:browser` + embedded focus → Back keys go to the hung process → no Back at all, then an input ANR dialog for the whole app.
- Partly inherent to SCVH (focus can't be shared, transfer is async). Mitigation: never take focus on show; on Qt text-input focus, synchronously drop panel focus and re-issue `showSoftInput` after `onWindowFocusChanged(true)` on the activity; route Back from the panel/embedded window to QML only when no IME is visible.
- API 30: SurfaceView→embedded focus transfer (`grantEmbeddedWindowFocus`) is Android 12 (API 31) as far as I know → on API 30 the page may get **no keyboard at all**. Verify on an Android 11 device before claiming 30+ support.

---

## MAJOR

### M1. Pass-through (drawer edge swipe) exists only on API 34+
`BrowserProcessService.java:235` and `RemoteWebView.java:551`: `if (SDK_INT < 34) return`. On 30–33 the embedded window eats all touches over the page → drawer edge-swipe and other host-view gestures are dead over the Browser on Android 11–13. Low-end devices, the ones the memory work is for, are mostly on 11–13. Inherent: `setTouchableRegion` on SCVH root is 34+. Workaround: on <34 shrink the SurfaceView/embedded rect to leave the strip out, or stay in-process below 34.

### M2. Chromium UI that needs an Activity breaks in a Service-context SCVH (verify each)
WebView context = `ContextThemeWrapper(BrowserProcessService)` (`BrowserProcessService.java:~190`), not an Activity.
- JS `alert/confirm/prompt/beforeunload`: no `onJsAlert` etc. override; Chromium's default `JsDialogHelper` refuses non-Activity contexts and cancels → `confirm()` returns false silently. Regression vs in-process.
- `<select>` popup, `<input type=date/time/color>` pickers, text-selection floating toolbar (copy/paste), autofill dropdowns: dialogs/PopupWindows need a window token. In SCVH that's the windowless token → BadTokenException/crash or no UI depending on Chromium/OS version. High likelihood at least one of these is broken; device-check each.
- Autofill/password managers and **passkeys/WebAuthn** (Credential Manager needs an Activity) are lost.
- Deep links: `CustomSchemeUrlHandler.java:49,70` `startActivity` from the service context. `FLAG_ACTIVITY_NEW_TASK` is set (`UrlLoadingHelper.java:21`), so no crash. But Android 10+ background-activity-launch rules are now judged for `:browser` (same UID with a visible window → probably allowed). Verify `wc:`/`metamask:`/`intent:` links still open.
- Fixable piecemeal: override JS dialogs and forward them to QML over IPC; select/date pickers are not cleanly fixable without an Activity context.

### M3. Lifecycle: no pause, no host-visibility propagation
No `onPause`/`pauseTimers`/app-state handling anywhere (grep mobilewebview + Browser QML). In-process, Chromium followed its window's visibility. An SCVH root doesn't learn that the host activity stopped, so the page can keep rAF/WebGL/timers running while Status is backgrounded, until the cgroup freezer catches the cached pair. Many OEM ROMs/Android 11 don't freeze → battery drain. Fix: forward activity start/stop over IPC → `WebView.onPause/onResume`, root visibility GONE.

### M4. Respawn replays every tab, not just the visible one
`BrowserProcessClient.java:71` replays all registered `RemoteWebView`s. Each one gets `create` + `restoreState`/`loadUrl` (`RemoteWebView.java:173-221`). N tabs → N WebViews + N network reloads on every Browser re-entry. Background tabs also lose their live page 5 s after hiding (in-process kept them). Fix: create lazily on first show, cap live tabs.

### M5. Stale saved state restored after a crash (wrong history; privacy)
`mSavedState` is written only on idle-release (`RemoteWebView.java:254-258`) and never cleared or invalidated. It is preferred over `mLastUrl` in replay (`:200-211`). After a crash/LMK in a later session, the WebView restores the **old** history/page, not where the user was. It also brings back history the user cleared with `clearHistory`/clear browsing data. If that old page is a different origin, queued bridge responses (B2) go to the wrong site (account addresses/signatures leak to an unconnected origin). Fix: clear `mSavedState` once replayed; prefer the newest of (state, lastUrl); clear it on clear-history/data; never replay bridge messages.

### M6. Hung `:browser` is never detected; release can wedge forever
- `saveAndRelease` sets `mSaving=true` and waits for "saved" (`BrowserProcessClient.java:202-226`) with no timeout. A hung `:browser` main thread never answers → never unbound → Chromium memory is never reclaimed, and later idle attempts return early.
- No watchdog/ping: calls post to a dead main looper forever. The page is frozen, the UI looks alive, and the user gets an input ANR attributed to "Status".
- Fix: timeout on save → unbind anyway; ping/ANR watchdog → `killProcess` via a `:browser` pid exchanged in `hello` (pid is already sent, unused), then backoff respawn.

### M7. Sync Binder on Qt/main threads under a lock
`BrowserIpc.send(..., oneway=false)` for every call (`BrowserProcessClient.java:224-230`, `RemoteWebView.java:223-227`), called from the Qt thread (JNI from the backend) and the Android main thread while holding `mLock`. `replayInto` issues ~10 sync transactions under `mLock`, which blocks the Qt thread if it calls in meanwhile. Normally µs, but on a thrashing low-RAM device (`:browser` paging/GC) each one stalls a Qt frame. A frozen target makes them fail (Android 12+) and the call is just logged "lost". Make them oneway (ordering per binder object is kept for oneway from one thread; use one sender thread).

### M8. The UI process still pays for HWUI it no longer needs
`WebViewWindow.java:159`: the panel always has `FLAG_HARDWARE_ACCELERATED`. With remote content the panel draws only a SurfaceView hole, yet the UI process brings up a HWUI renderer, EGL/Skia context and BLAST buffers. `WindowSurfaceMode` exists precisely to avoid this ("four full-screen buffers and a Skia device"). It is also one extra translucent full-screen layer for composition (HWC/GPU, battery). Fixable: no HW flag + one clear software frame for remote content, or make the SurfaceView a child of a non-HW window.

### M9. Memory reality check
- While the Browser is open, total device PSS does not go down. It goes up: a second app process (ART + app classes ~15–30 MB), SCVH buffers in `:browser`, and the HWUI panel in UI (M8). The UI-process PSS number improves, LMK/user-visible "app memory" while browsing does not.
- The real win is reclaiming Chromium after leaving. The price: a cold start on every re-entry (fork + WebView provider load, roughly 300–800 ms on low-end + page reload over network) and the B2 state loss. The 5 s TTL makes tab hopping thrash.
- Positive: background LMK can now kill Chromium without killing Status; WebView provider updates kill only `:browser`.

### M10. Kill/rebind race in service teardown
`BrowserProcessService.java:66-72`: `onUnbind` posts `killProcess` 200 ms later. A rebind in that window creates a new Service instance in the same, still-alive process. The pending kill then kills it → `onHostGone` → second cold start, with a visible reload flash. The queued-call bind path (hidden-tab `loadUrl`, clear-data) makes it likelier than "user reopens within 200 ms". Also the hard kill 200 ms after `destroy` can drop not-yet-committed localStorage/IndexedDB/cache writes (CookieManager flush is sync, the others aren't). Fix: cancel the kill if `onBind`/`onCreate` runs again; flush then let AMS reap.

### M11. API 28–29 path regressed by df2843a; dual path untested
See df2843a section. In general: three behaviours to keep working (in-process activity view, in-process own panel, remote SCVH), switched by `SDK_INT` and `WindowSurfaceMode` (`setRemoteProcess` is only called when the surface is taken; the profiling `STATUS_WINDOW_MODE=hw` build silently goes in-process, `WindowSurfaceMode.java:86-95`). So profiling builds don't exercise the shipping path. No automated tests cover IPC replay, death or focus.

---

## MINOR

- **Swipe-indicator hide animation (owner symptom)** — not traced to a line. Candidates: (a) the panel/embedded window taking window focus makes it the insets/system-bar control target, so the activity's bar animations stop applying; (b) per-frame `updateViewLayout` of the panel during navbar animation (`RemoteWebView.java:525` → `WebViewWindow.update`) contending with the `StatusIndicatorBand` sub-panel relayouts (`NativeIndicatorHelper.java:~60`). Check `dumpsys window` for the insets control target after opening the navbar.
- Freeze snapshot: <34 falls back to software `WebView.draw` (`WebViewCore.java:~690`), slow and often blank for a HW-composited page. The bitmap crosses Binder as a ~10 MB ashmem blob per freeze.
- Default UA: the UI-process answer is the prefs cache (`MobileWebView.java:51-60`). First run after install / WebView update → empty or stale until `hello` arrives; requests made before that carry the wrong UA. `isClearSiteDataSupported` is likewise false until the first hello (`DataClearManager.java` client mode).
- WebView data dir: `:browser` uses the default dir; the UI process must never touch WebView/CookieManager/WebStorage on 30+, or the second process throws "same data directory from more than one process". Holds today (ProbeRequest/DataClearManager run in `:browser`) but it is an unguarded trap. Adding `setDataDirectorySuffix` would log users out of every dApp on upgrade.
- Config changes: the SCVH is created once with the attach-time `Display` (`BrowserProcessService.java:~205`). Foldable inner/outer display switch, external display, density change → wrong scale until respawn. Dark-mode/uiMode changes are not forwarded to the SCVH views.
- Rotation: a few frames of stretched/cropped stale content until the async resize round-trip lands.
- Activity recreation (config not in `configChanges`, e.g. `fontWeightAdjustment`, `grammaticalGender`): `RemoteWebView` keeps the old `mActivity`/`mHostContent` → leaked window, invisible page (inherited from the panel design).
- `BrowserProcessService.mClient` is written on main and read from download/probe threads without `volatile`.
- Oneway callback flood (download progress, load progress, find results) into the UI's async binder buffer (~512 KB). When exhausted, sends fail and callbacks are silently dropped (`BrowserIpc.send` only logs).
- `saveState` Bundle over oneway Binder: large histories can exceed async-buffer limits → `TransactionTooLargeException` → state silently lost.
- Edge strip over the page: non-swipe taps there replay to the Qt view, not the page (`NativeSwipeHandlerHelper.findPassthroughTarget`). A dead strip on the page's left edge, inherited.
- QML toasts/notifications are always under the page (ZOrderOnTop SurfaceView above the panel above the activity), inherited from the panel design. Only hide/freeze helps.
- Accessibility: SCVH supports embedded a11y on 30+, but TalkBack traversal across Qt window → panel → embedded window is unverified. Test before shipping.
- Still missing (pre-existing, but each now needs IPC to fix): `onShowFileChooser`, `onPermissionRequest`/geolocation, `onShowCustomView` (fullscreen video), HTTP auth, SSL error UI.

## Security
- No regression found. Service `exported="false"` (status-app manifest diff). Both Binder receivers reject foreign UIDs (`BrowserIpc.java:55-58`). Renderer stays in Chromium's isolated sandbox.
- `:browser` runs with the full app UID (can read the app data dir: status-go DB, keystore files), same as Chromium's browser side had in-process. Marginal gain: Nim/Qt key material is no longer in the same address space.
- Trust boundary unchanged: origin for `onWebMessageReceived` is computed in `:browser` and trusted by UI. Same UID, acceptable.
- Real issue: bridge responses are not bound to the origin/document that asked (pre-existing). The replay queue (B2) plus stale restore (M5) can deliver them to a different page.

---

## df2843a (unbuilt focus experiment)
- `WebViewWindow.setVisible` now always calls `setFocusable(visible && focusable && mFocusOnShow)`. For the **in-process** path `mFocusOnShow=false` → every re-show (after any popup/freeze) actively **drops** focus the user's touch granted. Before, it only cleared when `!visible || !focusable`. API 28–29 page inputs lose the keyboard after every popup. Regression.
- Remote path: every show with `grabFocus=true` moves window focus off the Qt window. Qt sees deactivation; hardware keys and Back go to the panel/embedded window (worsens B4's Back behaviour); the panel becomes the insets control target (possible link to the swipe-indicator symptom). Correctness depends on native passing `grabFocus=false` exactly while a Qt field owns the IME, but "owns the IME" is false right after the IME-hide button → address-bar re-tap race gets worse.
- "Dropping is idempotent" calls `onFocusableChanged(false)` → `SurfaceView.clearFocus()` on every outside touch, i.e. a WM embedded-focus IPC per Qt tap. Combined with `onWindowFocusChanged(true) → requestFocus()` (`RemoteWebView.java:137-144`), there is ping-pong risk if the embedded grant is refused mid-relayout.
- Doesn't help API 30 (no embedded focus transfer, if my reading of S+ is right).
- Verdict: treats the symptom (first-tap IME) by stealing focus pre-emptively; trades a page bug for Qt-side bugs. Better: grant on `onTouchDown` but **delay Chromium's IME show until the grant lands** (can't control Chromium), or accept a 2-tap first focus.

---

## Would a different design avoid this?
- **Out-of-process Activity instead of SCVH embedding** (`:browser` Activity, native chrome or a minimal native toolbar; signing modals via returning to the Qt activity or a native sheet). Avoids B1, B4, M1, M2, M8 entirely: real focus/IME, Back, dialogs, select, autofill, passkeys, a11y, fullscreen, file chooser, no crop/z-order/freeze hacks, works on 28+. Costs: QML chrome can't live there without loading Qt; activity transitions; dApp signing UX crosses activities. Still needs B2/B3/M5 handling (IPC bridge, death).
- **Custom Tabs for plain browsing, WebView only for dApps**: plain pages cost Status zero memory and get every Chromium feature. Only dApp tabs need the provider/bridge. Shrinks the problem, doesn't remove it.
- **Keep SCVH but fix the policy layer**: relayout once settled (B1), `onRenderProcessGone` + backoff + foreground-only respawn (B3), TTL on "left Browser"/`onTrimMemory` instead of 5 s visibility (B2), lazy per-tab create (M4), non-HW panel (M8), oneway IPC (M7), save timeout + watchdog (M6). B4, M1 (<34) and M2 remain inherent to SCVH; they are the deciding cost.
- Inherent to any out-of-process WebView: B2-class state loss on death, bridge correlation, cold-start latency on re-entry.
