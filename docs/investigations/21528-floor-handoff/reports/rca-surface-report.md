# rca-surface: #6 nav-bar indicator lost after Browser, #7 flash on Browser open (WIP)

Scope: spike/floor-ship 1e0ae314cb, lever a5cfbbf46c "perf(android): no HWUI buffers for the activity window behind Qt's SurfaceView" (files: WindowSurfaceMode.java new, NativeIndicatorHelper.java, StatusQtActivity.java, AndroidManifest.xml).
Work dir: spikes-floor/rca-surface/ (rca.sh = Redmi wallet→drawer→Browser(screenrecord)→drawer→Wallet with screencap + dumpsys window/SurfaceFlinger/gfxinfo/input/activity top per stop; tools/frames = AVFoundation frame-luma extractor).
Runs: w0 = app.status.fwinr (s-win release APK, same lever), s1 = app.status.ffloordbg (ship, debuggable), d1 = app.status.frcs (diag build: ship + timing logs + #6 fix, branch spike/floor-rca-surface afe08d3d57, scratch), b1 = app.status.fbase (master 518d92d76f), s2-hw = ffloordbg with STATUS_WINDOW_MODE=hw (same build, old window).

## Mechanism (what the lever does to the window)
- Before `super.onCreate`, `WindowSurfaceMode.beforeCreate` sets the window TRANSLUCENT and calls `Window.takeSurface(ONE_CLEAR_FRAME)` (WindowSurfaceMode.java:84-91). ViewRootImpl.setView then skips `enableHardwareAcceleration` ("If the application owns the surface, don't enable hardware acceleration", AOSP ViewRootImpl.java:1525) and `draw()` returns early ("The app owns the surface, we won't draw", :5550). No HWUI renderer, no VRI gralloc buffers except one linear CPU buffer from the clear frame (needed: SurfaceFlinger derives the input frame from the buffer).
- Consequence: no View in the activity window is drawn. The swipe indicator (NativeIndicatorHelper ImageView) is therefore moved into its own software `TYPE_APPLICATION_PANEL` window "StatusIndicatorBand" (NativeIndicatorHelper.java:41-77; needs `application hardwareAccelerated=false`, activity `true`).
- WebView needs HWUI (draw functor). `afterCreate` installs a global-layout watcher; the first time a WebView/TextureView is VISIBLE (`MobileWebView$3`, logged "window drawn by HWUI from now on") it posts `drawViews` (WindowSurfaceMode.java:133-147): listeners `beforeWindowReadd` → `wm.removeViewImmediate(decor)` → `takeSurface(null)` → `wm.addView(decor, attrs)` → listeners `afterWindowReadd`. One way: the window stays HWUI for the rest of the process (back to 4 VRI buffers, base memory).
- Gesture exclusion: unaffected by the flip in itself (exclusion rects live on the View and are re-reported on attach); drawer swipe works after the flip (w0 s4, s1 s6).

## #6 Indicator disappears on Browser and never comes back
Regression: yes, caused by a5cfbbf46c (master has no flip; master check: b1, see Evidence).

Root cause: listener snapshot taken twice in `drawViews`.
- WindowSurfaceMode.java:141 `for (Listener l : new ArrayList<>(sListeners)) l.beforeWindowReadd();`
- NativeIndicatorHelper.beforeWindowReadd → `unhostBand()` (NativeIndicatorHelper.java:91-98), which ends with `WindowSurfaceMode.removeListener(this)` (:97).
- WindowSurfaceMode.java:146 `for (Listener l : new ArrayList<>(sListeners)) l.afterWindowReadd();` builds a NEW snapshot, which no longer contains the helper → `afterWindowReadd()`/`host()` never runs.
- Result: the indicator `FrameLayout` (container + ImageView) is detached from the band and never added to the content view. Later `updateLayout` calls (NativeIndicatorHelper.java:139-166) take the `!inBand()` path and just `setX/setY/setVisibility` a view with no parent, and nothing calls `host()` again (only `createViews` and `afterWindowReadd` do) → invisible for the rest of the process, on every screen.

Evidence:
- `dumpsys activity top` (s1): before the flip the content view holds QtRootLayout + swipe overlay `View` and a separate "StatusIndicatorBand" ViewRootImpl (gfxinfo: 2 ViewRootImpls, band 3 views); after the flip `android:id/content` = QtRootLayout, swipe overlay View, `org.mobilewebview.MobileWebView$3` — no indicator FrameLayout/ImageView, 1 ViewRootImpl. Same in w0 (gfxinfo 13+3 views → 14 views, Total ViewRootImpl 2 → 1).
- `dumpsys window`: Window "StatusIndicatorBand" present at s1-wallet (`(0,1067)(fillx320) ty=APPLICATION_PANEL`), absent after the flip; SurfaceFlinger layer `StatusIndicatorBand#…` gone.
- Video w0/browser.mp4 (fwinr release): the indicator is visible on the Browser screen until the flip frame (left strip luma 239 → 198 at t=7.28 s), then never again (s3/s4/s5/s6 screenshots, drawer open and closed).
- d1 (diag build with the single-snapshot fix): see "Diag build" below.

Fix (only option worth listing; zero memory/CPU impact): take one snapshot for both passes in `drawViews` (`final List<Listener> ls = new ArrayList<>(sListeners); for (l : ls) before…; …; for (l : ls) after…`), or keep the helper registered across a re-add (unregister only in `cleanup()`). 2 lines, no risk.

## #7 Display flashes when the Browser opens
Regression: yes, caused by a5cfbbf46c (master: WebView attaches to the already-HWUI window, no window removal).

What the "flash" is: a full-screen BLACK frame sequence, not a white flash. w0/browser.mp4 (fwinr release, Redmi): tap → Browser page with the indicator (frames 2-12), indicator band removed (t=7.28), whole display black from t=7.36 s (status bar icons switch to light-on-black, then hidden) until t=9.94 s when the new HWUI window shows the Browser → ≈ 2.6 s of black on the Redmi (release). Logcat (system_server): old window `a3208df` removed 10:54:43.604 (`Setting back callback null`, `removeSurfaceViewPkg`), new window `bc0a1e4` focused 10:54:44.53 with `frame=[0,0][0,0]` (not drawn yet), SurfaceView added 10:54:44.86; app HWUI frames of 1.9 / 2.6 / 4.1 / 4.9 s in gfxinfo after the flip.

Mechanism:
1. `removeViewImmediate(decor)` destroys the activity's only window. The activity is opaque (no SHOW_WALLPAPER), so SurfaceFlinger has nothing to compose for the app → black. The Qt SurfaceView is a child of that window and goes with it (`surfaceDestroyed` → Qt `setSurface(null)`, render thread drops its swapchain while the UI thread waits).
2. `addView(decor)` creates a new ViewRootImpl/window. WM keeps the new window hidden until its first frame is drawn (DRAW_PENDING → finishDrawing).
3. That first frame is expensive and serial:
   - first HWUI renderer in the process (the window surface was taken since launch, so Skia-Vulkan context/pipelines are created now);
   - the new window's first relayout is a WMS sync: SurfaceView.updateSurface → `handleSyncBufferCallback` waits on `SyncBufferTransactionCallback.waitForTransaction()` (CountDownLatch, no timeout, AOSP SurfaceView.java:1304-1320, 1196-1202) until Qt queues its first buffer on the NEW SurfaceView surface — Qt has to re-create its swapchain and render the whole scene (incl. the Browser QML) first; the UI thread blocks meanwhile. Logcat: `SurfaceSyncGroup: Failed to receive transaction ready in 1000ms. Marking SurfaceSyncGroup(wmsSync-VRI[StatusQtActivity]#12) as ready` (s1);
   - the WebView re-attaches to the new window and draws its first functor frame (chromium GPU init) inside the same first HWUI frame.
4. Only then WM shows the window → content appears. Qt never paints into the gap: the gap is window-level, not Qt-level.
- Diag timings (d1): see below.

## Master check
(b1 / s2-hw: pending)

## Diag build (d1)
(pending)

## Fix options
(pending — draft)

## Memory impact reference
Lever saving (s-win, Redmi release n=3): login −8.0 / post −14.0 MB PSS, dumpsys gpu −19.6 / −17.8, EGL −4.7/−3.9; it already reverts to base after the first Browser visit (one-way flip).
