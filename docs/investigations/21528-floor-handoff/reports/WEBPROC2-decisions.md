# webproc v2 — grilled decisions (owner, 2026-11-11)

Rule: master parity, new mechanism. Only new behaviour = rules for killing/restoring WebViews. New features → logged tasks.

Gate: reclaim2 numbers (in-process destroy vs out-of-process reclaim) + owner's Android ≤13 user share (Q9: remote path API 34+ only, in-process below?) before spawning.

## In scope — parity
- Drop crop. Resize only, frame by frame, coalesced to latest size (no backlog). Acceptance: no worse than master (fnomm) in side-by-side recording. Layout rect = content area below address bar; never under QML chrome.
- Keyboard/Back spec: (1) IME hide / Back with IME up only hides it, never navigates; (2) Back without IME: page history back, else master behaviour; (3) address bar tap always shows IME, also after dismiss; (4) same for page fields; (5) page tap after address bar moves focus to page. Two focus owners only (Qt window, embedded page); panel never focusable; focus moves only on touch; address bar reclaims focus before showSoftInput (wait for window focus change). API 30 without page IME → in-process fallback. Drop df2843a.
- Swipe indicator hide animation (portrait only; landscape has no drawer): bisect fnomm vs fwebwin vs new first.
- Activity-dependent features: verify each vs fnomm (dialogs, select, autofill, file chooser, fullscreen video, permissions, window.open, TalkBack). Only what works on master must keep working (forward to existing QML popups where master uses them).
- Config: forward dark mode/density.
- UI→:browser calls oneway, never block Qt/main thread.

## In scope — forced by killing
- Signals from QML: "Browser left" (section switch, app background) vs "covered by popup" (never starts timer). Replace 2 s freeze heuristic.
- Kill after 30 s hidden (constant). Exceptions: pending dApp request, running download, open file chooser/permission dialog → kill after they finish. Same timer in background.
- Media keeps page alive: injected JS (media element play/pause/ended capture listener + AudioContext state) → while active, no kill timer; timer starts when media stops.
- Restore: saveState after every navigation (big bundles via file); clear saved state once replayed / on clear history/data; newest of state vs last URL wins; lazy per-tab restore on show; nothing respawns while hidden.
- Pending dApp requests rejected (popups closed) on kill, death, navigation/reload; never replayed.
- Unexpected death while visible: onRenderProcessGone + binder death → respawn+reload once; second death within 30 s → stop (page blank, logged).
- Teardown race: cancel pending kill on rebind; flush storage, let AMS reap.

## Logged as tasks (not built)
Crash placeholder UI; freeze heartbeat + Wait/Reload; onPause/pauseTimers; SurfaceSyncGroup; full bridge origin/navigation tagging; panel non-HWUI; callback throttling (unless overflow seen); foldable/external display SCVH recreate; select/autofill/passkeys if missing on master. No Chrome Custom Tabs.

## Agent process
One agent, narrow brief, exit files + Monitor, lead watchdog (10 min idle ping). Lean verification: one device check per item; recordings only for resize + indicator. Samsung function, Redmi memory/CPU/recordings, Lenovo landscape/rotation only. Order: indicator bisect → resize+focus (report) → kill rules/restore/media → death handling (report) → pending-request rejection → Activity-feature checks → memory pass. One commit per spec item. Stop and report if core mechanism blocked.
Inputs: webproc-adversary.md, webproc clones (mobilewebview-webproc a4e0050, spike-floor-webproc 4cb40e0e56), owner findings 1–7.

## Q9 resolved (owner: ~30% of users on Android ≤13)
- Remote path on API 30+ (31+ if API 30 page IME fails; verify first on android-emu API 30). API 28–29 (or 28–30) in-process.
- Below 34: drawer edge strip moves into its own small window stacked above the browser panel/embedded page (reuses NativeSwipeHandlerHelper logic; tap replay to a remote page forwarded to :browser). API 34+: keep setTouchableRegion pass-through.
- First agent step: emulator checks API 30 (IME in embedded page) and API 33 (edge-strip window).
