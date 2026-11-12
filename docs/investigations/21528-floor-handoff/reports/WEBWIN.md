# Brief: MobileWebView in its own window on Android (agent "webwin")

Read first: /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/COMMON.md. It applies in full, except where this brief overrides it. Also read the commit-7 analysis: /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/rca-surface-report.md.

## Background
Draft PR status-im/status-app#23106, commit "perf(android): no HWUI buffers for the activity window behind Qt's SurfaceView" (mobile/android/qt6/src/app/status/mobile/WindowSurfaceMode.java): the activity window takes its surface (`Window.takeSurface`), so ViewRootImpl never creates an HWUI renderer. Saves (Redmi release, n=3) login −8 / post −14 MB PSS, GPU −19.6 / −17.8 MB. The swipe indicator already lives in its own software panel window (NativeIndicatorHelper.java). The first time a WebView/TextureView becomes visible, `drawViews` re-adds the main window with HWUI, one way: ≈2.6 s full-screen black on the Redmi, and the saving is lost for the rest of the process.

The browser is a native `android.webkit.WebView` from the mobilewebview library (FetchContent in ui/StatusQ/CMakeLists.txt:454, repo https://github.com/status-im/mobilewebview.git, pin 004549ca35c696c319d2c359893720142ac4fc18). `MobileWebView.java:132` creates it, `:246` adds it to the activity root view (`mRootView.addView`), `:565` positions it with layout params + setX/setY to track the QML item.

## Goal (spike)
Host each WebView in its own window (e.g. a `TYPE_APPLICATION_PANEL` or sub-window attached to the activity token, HWUI enabled for that window only) that tracks the QML item geometry, so the main window keeps its taken surface and `drawViews` never runs for WebViews. When the last WebView is gone, its window is removed. Prove or disprove that this works well enough to ship.

## Questions to answer
1. Does it render correctly: position/size tracking while the QML item moves (section switch, drawer, rotation on the Lenovo tablet if useful), clipping, z-order vs Qt's SurfaceView and the indicator band?
2. Input: touch, scrolling, the back gesture and drawer swipe, the soft keyboard / IME for web form fields (the window must be focusable for text input), text selection handles.
3. QML popups over the browser (menus, wallet/dApp popups): today they are drawn in Qt's surface below the WebView too. Check how the browser handles that now (it may hide the WebView) and that the same works with the separate window.
4. Accessibility: does TalkBack reach the web content in the separate window?
5. Memory (A/B vs the PR tip, own packages, medians of ≥3): after login before Browser; Browser open; Browser left (back to wallet, +60 s). Report PSS TOTAL/native/EGL, `dumpsys gfxinfo` / `dumpsys meminfo` graphics, and GPU memory (`dumpsys gpu --gpumem` or `/sys/kernel/...` where available). Is the commit-7 saving kept after leaving the Browser? What stays (HWUI RenderThread context, Chromium)?
6. The black screen: screenrecord the first Browser open, A/B.
7. CPU: idle 60 s with the Browser open and after leaving it, A/B.

## Workspace (overrides COMMON's "fresh origin/master")
- status-app: worktree `~/Repos/status-desktop.worktrees/spike-floor-webwin`, branch `spike/floor-webwin` from `perf/21528-android-ui-memory-floor` at 51c123c2cf. Baseline = 51c123c2cf, same flags, own package.
- mobilewebview: clone it fresh into `~/Repos/status-desktop.worktrees/mobilewebview-webwin` (do NOT touch ~/Repos/mobilewebview, the owner's clone), branch `spike/webview-window` from the pinned commit. Build status-app against it with `-DFETCHCONTENT_SOURCE_DIR_MOBILEWEBVIEW=<your clone>` (or the equivalent the StatusQ CMake/mobile scripts allow; check `mobilewebview_java_dir.txt` in mobile/scripts/buildApp.sh so the Java sources come from your clone too). Commit in both repos.
- WindowSurfaceMode.findViewNeedingWindowDraw must no longer trigger for WebViews that live in their own window (TextureView elsewhere may still trigger it; note any such users).

## Devices (overrides COMMON's "Samsung is disabled")
- Redmi A5 and Samsung S21 FE are both available through lease.sh (`lease.sh run samsung ...`, serial `adb-RFCRC0FY4ZJ-dSNrUl._adb-tls-connect._tcp`; `svc power stayon true` is applied by the lease). Use the Samsung for functional/rendering/input checks and the Redmi for the memory A/B (it is the reference device). Same install rules on both: own packages only (`adb install -r`), never app.status.mobile / .debug / .pr. Samsung: simpleperf perf events are unsupported; run-as works only for debuggable packages.
- Another agent ("nomm") is building and measuring at the same time; the lease serialises builds and devices. Don't hold a device longer than your job needs.

## Deliverable
- Branches + commits in both repos. NO pushes, NO PRs, NO GitHub comments.
- Report: /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/webwin-report.md answering questions 1–7 (measured vs estimated marked), a GO/NO-GO with the blocking issues if NO-GO, and what a production version would still need. Write `webwin.final` next to it when done.
- Final message: ≤10-line summary with the verdict and memory numbers + report path.
