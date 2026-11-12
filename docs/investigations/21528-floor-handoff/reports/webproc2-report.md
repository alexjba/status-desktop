# webproc2: out-of-process Browser, master parity (agent webproc2)

Scratch dir: `spikes-floor/webproc2/` (below: `W2/`). Last APK: `W2/apk/v10.apk` (= optA branch, i.e. **with** option A; no APK of the no-A branch tip was built) (pkg `app.status.fwebproc`, pr release made debuggable, Qt 6.11.0). All commits GPG-signed, no pushes.

## Branches
- mobilewebview `~/Repos/status-desktop.worktrees/mobilewebview-webproc`, `spike/webview-process-v2` from a4e0050 (df2843a dropped). Option A is **not** on this branch (lead HOLD, owner tests v4 by hand):
  - eeec0b2 resize only, no crop (step 2)
  - 6a35f0e focus: panel never focusable, activity + page owners (step 3, panel design)
  - b28cb77 remote path API 31+, in-process below; `debug.mobilewebview.remote30` test hook (step 0/3)
  - 6caa60c edge strip window below API 34 (step 4)
  - 1b1763a kill rules (step 5)
  - fa8a345 restore from saved history files (step 6)
  - 0755095 death handling + one-way calls (step 7)
  - 9926f09 dark mode/density/font scale to remote pages (step 9, standalone)
- Option A kept separately: branch `spike/webview-process-v2-optA` = the same steps with f04d201 (page hosted in the activity window, Back/IME routing) underneath step 8/9 (9356a8a, 7b44b54). Drop or pick f04d201 after the owner's manual v4 test.
- status-app `~/Repos/status-desktop.worktrees/spike-floor-webproc`, `spike/floor-webproc2` from 4cb40e0e56:
  - a60ef03bd6 indicator clip in the band window (step 1)
  - ed7c7ba323 QML keepAlive while a dApp request is pending (step 5)
  - 63056c0797 drop pending dApp requests of a gone page (step 7)
  - Build glue (debuggable + FETCHCONTENT override) left uncommitted in build.gradle/buildStatusQ.sh; copy in `W2/build-glue.patch`.

## Option A (on hold, branch spike/webview-process-v2-optA)
My scripted Samsung runs with the panel design (v4) showed no page-field IME after an `adb input tap` (2 runs, `W2/sam/f5`, `f6`). The owner reports the keyboard does show on a real tap, so this may be an `input tap`/dumpsys artefact; the owner is re-testing v4 by hand and option A is on hold. My reading was that the window manager grants embedded focus only through a focusable host. Option A: the remote SurfaceView now lives in the activity's content view (z-on-top, like Qt's own SurfaceView; no HWUI needed), so the host is the activity window and the focus owners are exactly Qt window + page. The WebView panel is back to the webwin (in-process) version. Two more things were needed:
- the embedded window never gets Back (the system sends it to the host): a `Window.Callback` proxy routes Back to the focused page (`PageBackRouter`);
- Chromium asks for the IME on the tap that also brings the focus, before the grant: the UI process asks again (150/500/1000 ms) until the IME insets are up.
Side effect: no HWUI panel any more → EGL while open = post (8.5 MB), was +3 with wp5.

## Per item (device, evidence)
| # | item | result | evidence |
|---|---|---|---|
| 0a | API 30 page field IME (emu, wp5) | **no IME** → remote gated to API 31+ | `W2/emu/e3/ime.txt`, `07-typed.png` |
| 0b | API 30 edge swipe over page (wp5) | swallowed by page (expected) | `W2/emu/e3/logcat.txt` (touch down in :browser, no drawer) |
| 1 | indicator hide (portrait) | **pass**. fnomm hides it; fwebwin + wp5 left it at the open navbar edge in the Browser only (Wallet fine) → regression from webwin's band window: the image was drawn unclipped (logged img x=-12, clip=true, drawn anyway). Fix: image clip bounds. | before `W2/red/i1/sh-*.png`, probe `W2/red/r3/probe-crop.png`; after `W2/red/r4/sh-ind.png`, `W2/red/r5/sh-ind.png` |
| 2 | geometry: resize per frame, coalesced (1 in flight), no crop | **pass** (looks like fnomm; scroll to top after navbar works) | fnomm `W2/red/r3/ind-app.status.fnomm.mp4`, `top-app.status.fnomm.mp4`; v9 `W2/red/r5/ind-app.status.fwebproc.mp4`, `top-app.status.fwebproc.mp4` |
| 3 | focus/IME/Back 1–5 (Samsung, v9 = option A) | **pass all 5** (scripted; panel design v4 pending the owner's manual test) in a clean run: addr IME + after dismiss; page field IME; Back with page IME only hides it (page stays); Back without IME = history back; page tap after addr → page IME | `W2/sam/f12/ime.txt`, `kbd.txt`, `08-after-back-ime.png`, `10-back-history.png` |
| 4 | edge strip window < 34 | **pass** on API 30 emu (remote forced): edge swipe over page opens drawer 2/2, strip window present | `W2/emu/e4` (v1), `W2/emu/e5` (v9) |
| 5 | kill rules | **pass** (Redmi): left 25 s alive / 40 s gone; popup 40 s alive; back within 10 s cancels; background 40 s gone; playing mp3 left 40 s alive | `W2/red/r3/bp.txt`, `W2/red/r4/bp.txt`, logcat `letting it go`/`kept` lines |
| 6 | restore | **pass**: history restored after kill (process gone → return) | `W2/red/r3/13-k2-restored.png`, `restoring saved history` log |
| 7 | death | **pass**: kill -9 while shown → respawn 0.2 s + restore; 2nd kill 12 s later → blank page, stopped, logged | `W2/red/r4/10-kill1.png`, `11-kill2-blank.png`, logcat |
| 7b | pending dApp requests dropped on load/respawn | code only (QML); no dApp sign flow run (never signs) | — |
| 8 | Activity features | see below | `W2/sam/f16..f18` |
| 9 | dark mode/density | **pass** after theme rebase: dark on/off flips page like fnomm | fnomm `W2/red/r5/13-nomm-dark-on.png`; v10 `W2/red/r7/0{4,5,6}-dark-*.png` |
| 10 | memory/CPU | see table | `W2/mem/dev/runs-v10/`, `W2/mem/dev/runs/` (v4, 2 runs, swap-heavy) |

### Step 8 detail (Samsung, remote v9/v10; master from code, fnomm login aborted: unknown profile name on that device)
- alert/confirm/prompt/beforeunload: master shows Chromium's native dialogs (Activity context). Remote: Chromium cancels them silently (Service context). **Fixed** (9356a8a): forwarded to the UI process as AlertDialog. **Not device-verified**: the w3schools "Try it" buttons sit under the drawer edge strip (x<204 on the Samsung; same dead strip in master), and the app blocks data:, javascript: and cleartext localhost pages.
- `<select>`: **broken** in remote: tap focuses the select (orange ring), no popup (`W2/sam/f16/10-f-select.png`, f17, f18). Master shows a dropdown (Activity context). Not fixed (needs an Activity/window token for Chromium's popup) → logged task.
- date input: not reached reliably (OCR), assumed same as select (Chromium picker dialog) → logged.
- file chooser, fullscreen video, camera/mic permission: master has no `onShowFileChooser`/`onShowCustomView`/`onPermissionRequest` → not supported on master either → parity, nothing to do.
- window.open / target=_blank: same `onCreateWindow` → new tab path in both (not run on device).
- TalkBack: not tested (owner's phone). Note: on the Redmi, the uiautomator tree of the Qt window was stale (login-screen nodes) right after login with v1+ until the first Qt input; wp5 did not show this. Not root-caused → logged.
- autofill/passkeys: not tested → logged (spec already lists them).

## Memory / CPU (Redmi A5, v10 = option A build, n=3, reclaim2 harness; TOTAL = dumpsys `TOTAL PSS:` line, already incl. SwapPss)
| state | UI TOTAL | native | Dalvik | .apk | .so | EGL | `:browser` | device | Δ device vs post |
|---|---|---|---|---|---|---|---|---|---|
| post | 238.5 | 96.5 | 2.7 | 73.1 | 20.0 | 8.5 | – | 238.5 | 0 |
| open | 243.5 | 105.8 | 10.0 | 68.6 | 12.7 | 8.5 | 127.5 | 370.7 | **+133.1** |
| closed +60 s | 253.8 | 107.6 | 9.1 | 69.3 | 19.5 | 8.5 | gone (3/3) | 253.8 | **+15.3** |
- vs reclaim2 B (wp5): open +130.5 (UI +3, :browser +125), closed +13.5. v10: open +133 (UI +5, :browser +127.5), closed +15.3. Same within noise (±3 MB per-run deltas, TOTAL ±10 at post). EGL while open +0 (was +3 with the HWUI panel).
- Idle CPU, 60 s, UI ticks: post 3034/2997/2941, closed 3063/3031/3059 → +29/+34/+118 ticks (+0.5–2 % of a core). Baseline ~50 % of a core is the known provider-down-banner confound. `:browser` 0 (gone).
- Raw: `W2/mem/dev/runs-v10/B{1,2,3}/`, summariser `W2/mem/summ.py`.

## Logged tasks (not built)
- Forward JS dialogs (not on master): alert/confirm/prompt don't show on master either (owner checked fnomm with w3schools tryjs_alert on the Samsung). The forwarding commit 96070d1 was dropped from spike/webview-process-v2 (minimal scope); it's still on the optA branch as 9356a8a, adapted to option A.
- `<select>`/date/color pickers in remote pages: no popup (Chromium needs an Activity window token). Options: forward to a QML/native picker, or a transparent Activity in `:browser`.
- Closing an already-open dApp sign popup when its page dies/navigates (needs dapps-service plumbing: page requestId ↔ connector requestId). Today: results are dropped (never replayed), popup stays; reachable only via renderer crash/LMK since covered/keepAlive block the timer.
- Tap replay from the edge strip to the remote page (the strip is a dead zone over the page in master and on 34+ too).
- Qt a11y tree stale after login with the remote path (Redmi uiautomator); TalkBack check.
- Lenovo landscape/rotation check of the activity-hosted SurfaceView (not run).
- Freeze heartbeat/Wait-Reload, crash placeholder UI, onPause/pauseTimers, SurfaceSyncGroup, bridge origin tagging, callback throttling, foldable display recreate (from the spec's list).
- `debug.mobilewebview.remote30` hook in MobileWebView (spike only; remove for production).
- WebViewCompat.addDocumentStartJavaScript reflection in BridgeScriptInjector fails under R8 in this build (pre-existing, falls back to onPageStarted injection); the media keep-alive script uses the direct API.

## Known gaps / risks
- Step 3 changed the hosting design (above). The panel-less design still needs the freeze for QML popups (unchanged) and was not checked against the Lenovo rotation.
- Focus tests passed in one clean Samsung run (f12); an earlier run was disturbed by a real touch on the nav-bar Back button. The IME re-ask is timing-based (retries up to 1 s).
- Pending-request drop and keepAlive were not exercised with a real dApp request.
- File chooser/permission kill exceptions: N/A (master has neither).
- Kill exit: cookies flushed, 1 s grace, then the process kills itself (not "let AMS reap": an empty cached process would keep Chromium's heap until LMK).
