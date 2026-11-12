# webwin: Browser WebView in its own window (spike)

Verdict: **GO** (conditional) for productising: the mechanism and the measured wins hold; three input bugs found were fixed in the spike; web-form IME and popup-freeze with the panel still need a device check (Samsung locked before that run). The spike keeps the activity surface taken through and after the Browser (no window re-add, no black frame), keeps commit 7's GPU/EGL saving after leaving the Browser, and cuts idle CPU after leaving the Browser from 8.7 % to 1.9 % on the Redmi.

## Branches / commits (no pushes)
- mobilewebview clone `~/Repos/status-desktop.worktrees/mobilewebview-webwin`, branch `spike/webview-window` from 004549ca:
  - 310dc9d feat(android): host each WebView in its own panel window (spike) — new `WebViewWindow.java`, `MobileWebView.setOwnWindows()`, PixelCopy `ofWindow` freeze snapshot
  - e183fe4 fix: keep the host's gesture exclusions over the WebView window
  - c02e6eb fix: give the focus back to the activity on touches outside the WebView window
  - 46cdb66 / f52763d fix: only a touch on the WebView makes its window focusable
  - 97455aa fix: route Back to the WebView from its window
- status-app worktree `~/Repos/status-desktop.worktrees/spike-floor-webwin`, branch `spike/floor-webwin` from 51c123c2cf:
  - 5600a419bd perf(android): keep the activity surface taken when the Browser opens (spike) — `WindowSurfaceMode.beforeCreate` calls `MobileWebView.setOwnWindows(true)`; indicator band becomes `TYPE_APPLICATION_SUB_PANEL` so it stays above the WebView panel.
- Builds: pr release made debuggable, Qt 6.11.0, same flags, one package `app.status.fwebwin` for both (A/B by reinstall, same profile). APKs: `webwin/apk/{base,ww1..ww5}.apk`. status-app built against the clone with `-DFETCHCONTENT_SOURCE_DIR_MOBILEWEBVIEW` (build-script patch of buildStatusQ.sh, not committed); Java sources come from the clone via `mobilewebview_java_dir.txt` (verified in the build log and dex).

## Design (what the spike does)
- `WebViewWindow`: one `TYPE_APPLICATION_PANEL` per WebView, attached to the activity window token, `FLAG_HARDWARE_ACCELERATED` (HWUI for this window only; the app is `hardwareAccelerated=false`), `NOT_TOUCH_MODAL | WATCH_OUTSIDE_TOUCH | LAYOUT_IN_SCREEN | LAYOUT_NO_LIMITS`, `SOFT_INPUT_ADJUST_NOTHING`, fit-insets 0. Added on first show, removed in `destroy()`. `setGeometry` → `updateViewLayout(x, y, w, h)` relative to the activity window (content offset added). `setVisible(false)` → root GONE (window hidden, surface released).
- `WindowSurfaceMode.findViewNeedingWindowDraw` no longer sees WebViews (they are not in the decor tree), so `drawViews` never runs for them. Other users that would still trigger it: none found (no TextureView/WebView in the app's Java; QR uses CameraX via QrCamera without TextureView). The fallback stays for anything that still lands in the activity window, and for `STATUS_WINDOW_MODE=hw`.
- Three problems found on device and fixed in the spike:
  1. A window on top hides the system gesture exclusion rects of the windows below → edge swipe over the browser became a system Back (measured: `EdgeBackGestureHandler triggerBack`, app left the Browser). Fix: the panel mirrors the activity window's exclusion rects (`addOnSystemGestureExclusionRectsChangedListener`), and gestures that start on host views with elevation > 0 (the drawer edge strip) are routed to them.
  2. Focus: the topmost focusable window keeps key/IME focus, so a focusable panel made Qt text fields (address bar) show no keyboard and get no text (measured: IME `mFocusedWindowSoftInputMode=ADJUST_NOTHING`, keys delivered to `VRI[MobileWebView]`). Fix: the panel is `NOT_FOCUSABLE` until a touch on the WebView, and drops focusability on the next outside touch (`ACTION_OUTSIDE`); `setVisible` never grants focus.
  3. Back with the panel focused and no focused view went unhandled → the activity went to the background. Fix: the panel's root hands KEYCODE_BACK to the WebView (which forwards it to QML, as before).

## Q1 Rendering / geometry / z-order (Samsung S21 FE, measured)
- Renders correctly (status.app, google.com, duckduckgo.com). Window list during Browser: `StatusIndicatorBand` (sub-panel), `MobileWebView` (panel), `StatusQtActivity` — the activity window id is unchanged for the whole process (never re-added); `window drawn by HWUI` never logged; gfxinfo `Total ViewRootImpl 3` (activity, band, webview).
- Geometry tracking: the panel follows the QML item every frame while the drawer slides (logcat: relayout per frame, ~15 ms apart, x 0→28→56…→204, width 1053→876, `relayoutAsync=true`); screenshot shows the page resized next to the open drawer. Rotation: not tested (no Lenovo run).
- Z-order: panel above the Qt SurfaceView and the activity window; indicator band (now SUB_PANEL) above the panel (visible over the page on Redmi and Samsung screenshots). Clipping: the window is a rectangle at the item geometry; QML-side clipping of the item (e.g. rounded corners, partial overlap by QML content) is not reproduced — same as before (the old View was also rectangular above Qt).

## Q2 Input (Samsung, measured)
- Touch + scrolling in the page: OK (ww1+).
- Drawer edge swipe over the page: broken in ww1 (system Back), OK from ww3 (exclusion mirror + routing to the edge strip). Drawer opens, page reflows next to it, tap outside closes it.
- Qt address bar keyboard: broken in ww1–ww4 (focus held by the panel), OK in ww5 (keyboard shows, typed URL loads). Base: OK.
- Web form field IME: **not verified** (fn13 did not find the duckduckgo field by a11y text; fn14 aborted because the Samsung was on its lock screen). Mechanism: a touch on the WebView makes the panel focusable, so the IME should target the WebView; untested.
- Back key in the page: ww5 goes back in web history (duckduckgo → status.app); a second Back at the first page left the app to the previous task; base behaviour for the same sequence not compared (fn14 aborted).
- Text selection handles / long-press context menu: long-press on a non-link area showed nothing (as base for non-link targets); link long-press and selection handles not exercised — not tested.

## Q3 QML popups over the browser (Samsung)
- The browser freezes the WebView while QML popups are open (`freeze: isMobile && hasPopups`): snapshot shown in QML, native view hidden. With the panel, hidden = window root GONE; the freeze snapshot uses `PixelCopy.Request.ofWindow(webView)` (API 34+; software draw below 34). Base menu popup verified (fnbase: Browser menu sheet over a dimmed page snapshot). ww5 menu popup: **not verified** (fn14 aborted). The freeze path (hide window + PixelCopy ofWindow) is implemented but untested on device.
- The drawer (also an Overlay popup) works with the panel (Q2).

## Q4 Accessibility (measured with uiautomator, not TalkBack)
- uiautomator dumps the focused window: once the panel has focus, the dump contains the web content nodes (e.g. "Vaults withdrawals are now enabled", menu links on status.app). So the a11y tree of the separate window is exposed. While the panel is focused, a single-window dump does not show the Qt window (TalkBack uses all windows, so it should see both; real TalkBack navigation not tested — turning TalkBack on the owner's phone was avoided).

## Q5 Memory — Redmi A5, A/B base 51c123c2cf vs ww1, n=3 medians (measured)
TOTAL = PSS + SwapPSS; native = Native Heap PSS + SwapPSS; MB. post = wallet +120 s after login; bopen = Browser open +20 s (status.app loaded); bleft = back to Wallet +60 s.

| step | build | TOTAL | PSS | native | .apk | .so | EGL | dumpsys gpu (proc) |
|---|---|---|---|---|---|---|---|---|
| post | base | 236.6 | 236.1 | 95.4 | 73.1 | 19.2 | 8.5 | 26.5 |
| post | ww1 | 236.4 | 235.8 | 95.5 | 73.2 | 18.8 | 8.5 | 28.0 |
| bopen | base | 378.9 | 344.8 | 128.5 | 106.5 | 22.7 | 19.0 | 74.7 |
| bopen | ww1 | 383.9 | 348.8 | 128.7 | 105.1 | 22.4 | 17.8 | 66.9 |
| bleft | base | 381.3 | 349.1 | 131.4 | 107.5 | 22.7 | **20.1** | **67.4** |
| bleft | ww1 | 363.5 | 328.2 | 122.2 | 105.3 | 22.5 | **8.5** | **31.2** |

- Commit 7 saving after leaving the Browser: **kept**. ww1 bleft EGL = post (8.5) and process GPU memory +3 MB over post, vs base +11.6 EGL / +41 MB GPU (the one-way re-add). bleft TOTAL −17.8, native −9.
- While the Browser is open both builds pay the same Chromium cost (~+140 MB TOTAL over post; .apk +32 = WebView/Trichrome code, native +33, GPU +40).
- What stays after leaving (ww1): the WebView is kept alive by the Browser (hidden), its panel window stays added but GONE (VRI count 3, surface released; EGL back to baseline). Chromium heap/code (+27 native, +32 apk) stays — that is the reclaim experiment's target.
- Raw: `webwin/dev/redmi/runs/{base,ww1}/{post,bopen,bleft}N.{mi,gpu,gfx,win,sfl,png}`, `rows.csv`, `gpu.csv`; summariser `webwin/summ.py`. ww2–ww5 only change input/focus Java code (no memory-relevant change; not re-measured).

## Q6 Black screen on first Browser open (Redmi screenrecord, measured, n=3 each)
- base: full-screen black (mean luma < 25) for **0.35 / 0.35 / 0.40 s** at each first open (the drawViews re-add; shorter than the 2.6 s rca-surface saw on its release build, same mechanism).
- ww1: **0 black frames** in all 3 (min frame luma 85 = the dark status.app page).
- Videos: `webwin/dev/redmi/runs/{base,ww1}/browserN.mp4`; per-frame luma `webwin/fr/*.txt` (rca-surface frames tool).

## Q7 CPU — Redmi, idle 60 s, /proc stat ticks, % of one core, per run (measured)
| | post | Browser open | Browser left |
|---|---|---|---|
| base | 1.95 / 1.93 / 1.92 | 2.02 / 2.02 / 2.10 | **8.62 / 8.80 / 8.75** |
| ww1 | 1.87 / 1.90 / 1.92 | 2.03 / 2.07 / 2.02 | **1.93 / 1.97 / 1.93** |
- No regression; after leaving the Browser base burns ~6.8 % more CPU. Likely cause (not proven): in base the hidden WebView stays in a visible HWUI window, so Chromium keeps its renderer/compositor active; with the panel the whole window goes GONE and Chromium sees the window hidden.

## Functional A/B summary (Samsung)
| check | base | ww1 | ww5 |
|---|---|---|---|
| page renders, no black flash | flash (Redmi 0.35-0.4 s) | OK | OK |
| scroll in page | OK | OK | OK |
| drawer edge swipe over page | OK | system Back (broken) | OK |
| Qt address bar keyboard + typing | OK | broken | OK |
| Back in page history | n/a | leaves Browser | OK (history back) |
| QML menu popup over page | OK (frozen snapshot) | not run | not run |
| indicator band over page | OK | OK | OK |
| a11y nodes of the page | (same window) | exposed in panel window | exposed |

## What production still needs
- Fold the fixes into a clean mobilewebview API (opt-in own-window mode, set by the host), unit-test the pure parts (exclusion translation, focus state machine).
- Text selection handles / link context menu / file chooser / fullscreen video (`onShowCustomView`) and permission dialogs inside the panel: verify.
- Rotation and multi-window/split-screen (Lenovo), keyboard insets (the panel is ADJUST_NOTHING; Qt resizes the item, which moves the panel — verify web fields near the bottom stay visible).
- Freeze snapshot below API 34 falls back to software draw (WebView often draws blank in software): use `PixelCopy` on a SurfaceView-free path or keep the last frame; check minSdk 28 devices.
- Back semantics at the first page and with the panel unfocused: compare with base and align.
- Untested in the spike: web-form IME, QML popup freeze with the panel, link long-press menu, selection handles, rotation, TalkBack.
- QML-side clipping/overlap: any QML content meant to overlap the WebView region (toasts, banners) is under the panel, as before; keep using freeze for overlays.
- TalkBack pass with a real screen reader.
- Destroying the WebView (and its window) when the Browser is left would release the Chromium share as well (see reclaim experiment).

## Reclaim after close (Redmi, ww6 = ww5 + spike debug hooks, n=3, measured)
Hooks (status-app commit 5ac46af23f, do not merge): `WebwinDebug` polls `<files>/webwin-step`; steps run 10–15 s apart after "Browser left +60 s" (S0):
S1 BrowserLoader inactive (destroys BrowserLayout → all WebViews + their windows; window count 1→0 confirmed) + `collectGarbage()` + `trimComponentCache()`; S2 `mallopt(M_PURGE_ALL)`; S3 `Application.onTrimMemory(RUNNING_CRITICAL)` on the main thread + `System.gc()`; S4 `MADV_PAGEOUT` on WebView/Chrome mappings from /proc/self/maps.

Per-step deltas, MB, median of per-run deltas (TOTAL = PSS + SwapPSS; native/dalvik/unknown incl. swap):

| step | TOTAL | PSS | SwapPSS | native | Dalvik | Unknown | .apk | .so | EGL | GPU |
|---|---|---|---|---|---|---|---|---|---|---|
| S0→S1 unload + gc + trim cache | −9.2 | −6.0 | −3.2 | −5.0 | −1.1 | −0.5 | +1.0 | 0 | 0 | −0.3 |
| S1→S2 M_PURGE_ALL | −0.6 | −0.2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | −0.9 |
| S2→S3 onTrimMemory + System.gc | −2.5 | −2.2 | −0.2 | −1.9 | −0.3 | 0 | 0 | 0 | 0 | 0 |
| S3→S4 MADV_PAGEOUT | +0.3 | +0.1 | 0 | 0 | +0.2 | 0 | 0 | 0 | 0 | 0 |
| **S0→S4** | **−12.5** | −8.9 | −4.7 | −7.0 | −1.3 | −0.2 | +1.0 | −0.1 | 0 | −1.2 |

- How much of the Browser increase comes back: run 1 (the only run with a clean pre-Browser baseline; runs 2–3 restored the Browser section at app start, so their "post" already had Chromium loaded): post 243.0 → S0 401.4 (+158) → S4 375.9 → **−25.5 MB, ~16 % of the increase**. Still above post at S4: native +19.5, .apk +45 (WebView/Trichrome code + Browser QML/JS code; S4 pageout did not move it — the WebView APK pages are shared with other processes, so PSS stays and MADV_PAGEOUT does not evict shared pages), Unknown +12.6 (Chromium PartitionAlloc/V8 kept by the process-global WebView runtime, which Android never unloads once initialised).
- smaps attribution (median, Pss+SwapPss): native 120.8 → 114.1, WebView code 35.0 → 36.1, Chromium-named anon 11.9 → 11.6, Java 30.9 → 30.7; see `webwin/smapcat.py` on `webwin/dev/redmi/reclaim`.
- CPU (ticks in the 10 s after each step, idle ≈ 20 ticks/10 s = 2 %): S1 49/56/57 (≈ +0.35 s of one core above idle: QML teardown + GC), S2 22/22/23 (≈ idle), S3 25/26/26 (+0.05 s), S4 21/22/23 (≈ idle). One-off costs, no lasting idle change (idle after S4 19/20/24).
- Re-open after S4: works (Browser UI, new-tab page) on all 3 runs; first change on screen 0.25–0.30 s after the tap, final frame by ~0.45 s (screenrecord, n=3). BUT the unload loses all open tabs (Browser comes back with 0 tabs, `Search or enter address`), so no web page was painted in the re-open; time to first web paint after a cold WebView start was ~3.0 s in run 1's first open (cold Chromium) vs ~0.4 s when Chromium was warm (runs 2–3).
- Conclusion: in-process reclaim after closing the Browser is small (≈12–25 MB of ~150) and costs the user's tabs; most of the increase is Chromium's process-global runtime and shared code that cannot be unloaded in-process. Big wins need the WebView out of the UI process (WebView in a separate process is not supported for app WebViews; would need a separate activity/process hosting the browser) or not starting it. Raw: `webwin/dev/redmi/reclaim/run{1,2,3}/` (mi, gpu, smaps, png, open/reopen mp4), `cpu.csv`.
