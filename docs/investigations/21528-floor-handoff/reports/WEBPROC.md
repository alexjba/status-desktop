# Brief: Browser WebView in its own process (agent "webproc")

Read first, in full: COMMON.md (same dir) — it applies except where this brief overrides it. Then webwin-report.md (same dir): your predecessor spike, its branches, build setup and device lessons. Also memory file /Users/alexjbanca/.claude/projects/-Users-alexjbanca-Repos-status-desktop/memory/project_21528_browser_chromium_cost.md.

## Background
Opening the Browser adds ~140–158 MB PSS to the UI process (Chromium runtime). In-process reclaim gives back only ~12.5 MB. Owner goal: keep Chromium out of the UI process while the Browser looks and behaves exactly as today — QML chrome (tabs, address bar, wallet menu, dApp popups) unchanged, only the WebView moves.

## Design to prove
- A `:browser` process (`android:process=":browser"` Service, bound via AIDL) owns the `android.webkit.WebView` + mobilewebview's Java logic and renders it with `SurfaceControlViewHost` (API 30+). It sends the `SurfacePackage` over Binder.
- The UI process places a `SurfaceView` (own window or the webwin panel window — your call, keep the activity surface taken as webwin does) at the QML item rect and calls `setChildSurfacePackage`. Geometry tracking as in webwin.
- mobilewebview's Java↔native surface (loadUrl, evaluateJavascript, postMessage/injected scripts, ~15 callbacks: navigation, title, history, progress, web messages, JS results, back, new window) becomes one AIDL interface; a Java proxy in the UI process keeps the existing native/C++/QML API unchanged, so the dApp provider path works untouched.
- The UI process must never load a WebView (one process per WebView data dir); `:browser` keeps the default data dir so existing cookies/localStorage stay.
- Lifecycle: leaving the Browser (or a short idle timeout; make it a constant) unbinds and the process is killed; reopening restores the URL/history (tabs restore via the existing session state is enough; note gaps).
- API < 30 (minSdk is 28): keep the in-process path as fallback; just gate it, don't test it.

## Questions to answer
1. Renders correctly, tracks geometry (section switch, drawer), z-order vs Qt surface and indicator band.
2. Input: touch/scroll, edge back gesture + drawer swipe (webwin's gesture-exclusion fix), system Back routed to the page, soft keyboard on a web form field (focus handover to the embedded window: `SurfaceControlViewHost`/`SurfaceView` focus APIs, API-level quirks), long-press/selection.
3. QML popups over the page (wallet menu, dApp connect/sign popups): today's hide/freeze behaviour still works.
4. dApp bridge end-to-end: open a dApp, connect wallet, a sign request popup appears (cancel it — never sign/send anything real).
5. Memory (Redmi, own package, medians of ≥3, A/B vs webwin tip 5600a419bd built same flags): UI-process PSS (TOTAL/native/EGL/GPU) post-login, Browser open, Browser left +60 s; `:browser` PSS while open; confirm it is gone after leaving. Total device-side cost while open (UI + :browser) vs today.
6. Time: tap Browser → first page painted, cold (`:browser` not running) and warm, A/B.
7. CPU: idle 60 s with the Browser open and after leaving, A/B (both processes).

## Workspace (overrides COMMON's "fresh origin/master")
- status-app: new worktree `~/Repos/status-desktop.worktrees/spike-floor-webproc`, branch `spike/floor-webproc` from `spike/floor-webwin` at 5600a419bd (NOT 5ac46af23f, the debug-hook commit).
- mobilewebview: new clone `~/Repos/status-desktop.worktrees/mobilewebview-webproc` (clone from the webwin clone or GitHub), branch `spike/webview-process` from `spike/webview-window` 97455aa. Never touch `~/Repos/mobilewebview` or webwin's clone.
- Build like webwin (its scripts in spikes-floor/webwin/, `-DFETCHCONTENT_SOURCE_DIR_MOBILEWEBVIEW` + `mobilewebview_java_dir.txt`), always via the buildbox lease. Own package: BUNDLE_IDENTIFIER app.status.fwebproc. Where the AIDL/Service/manifest entry lives (mobilewebview vs status-app manifest) is your call; note it.
- Your scratch dir: spikes-floor/webproc/.

## Devices
- Function checks: Samsung S21 FE (`lease.sh run samsung ...`, serial adb-RFCRC0FY4ZJ-dSNrUl._adb-tls-connect._tcp) or Lenovo. Memory/time/CPU: Redmi A5. Check each device's API level first.
- The Samsung is the owner's personal phone. Before any input, verify the screen is unlocked and our app is in the foreground (dumpsys window / screenshot); if locked or another app is on top, abort the job — never type anything on a lock screen, never enter PINs, never interact with biometric prompts. Scripts must check foreground before every input batch. Profile setup per COMMON's Samsung/Lenovo notes (Save password + biometrics OFF, screenshot-verified).
- Same install rules: own packages only; never app.status.mobile / .debug / .pr.

## Deliverable
- Commits in both repos (GPG-signed, no Co-Authored-By). NO pushes, NO PRs, NO GitHub comments.
- Report: spikes-floor/webproc-report.md answering 1–7 (measured vs estimated marked), GO/NO-GO with blockers, what production still needs. Write `webproc.final` next to it.
- Final message: ≤10 lines, verdict + numbers + report path.
- If the core mechanism (embedding works with touch) fails early, stop and report rather than building workarounds.
