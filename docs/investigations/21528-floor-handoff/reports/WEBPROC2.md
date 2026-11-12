# Brief: Browser out-of-process, v2 — master parity (agent "webproc2")

Read first, in full:
1. COMMON.md (same dir) — applies except where overridden here (esp. "Waiting on long jobs").
2. WEBPROC2-decisions.md (same dir) — THE SPEC. Owner-grilled; don't re-decide anything in it.
3. webproc-adversary.md — code-level findings (file:line) on the current spike; many map to spec items.
4. WEBPROC.md (the v1 brief) — architecture, device safety rules, build setup. reclaim2-report.md for memory context only.

## Goal
Turn the webproc spike into a master-parity out-of-process Browser: the Browser looks and behaves exactly as master (fnomm = `app.status.fnomm`, PR #23106 tip with the in-process WebView, is the reference build on the devices), except WebViews now live in `:browser` and get killed/restored by the rules in the spec. Minimal diff. Anything master doesn't do → add to the "Logged tasks" list in your report, don't build it. If you find a new behaviour gap not covered by the spec, prefer parity with fnomm; if parity is impossible, stop and ask the lead (SendMessage "team-lead"), don't invent.

## Workspace
- mobilewebview: `~/Repos/status-desktop.worktrees/mobilewebview-webproc`, new branch `spike/webview-process-v2` from a4e0050 (df2843a is DROPPED — it regresses the in-process path).
- status-app: `~/Repos/status-desktop.worktrees/spike-floor-webproc` (has uncommitted build glue in build.gradle / buildStatusQ.sh from v1 — keep using it, don't commit it unless it's real), new branch `spike/floor-webproc2` from 4cb40e0e56.
- Build like v1 (scripts in spikes-floor/webproc/: run-build.sh, rbuild.sh), always via the buildbox lease. Package stays `app.status.fwebproc` (existing test profiles: Redmi + Samsung "Voluminous…"; the owner's "Sniveling Profu…" profile on the Samsung must never be touched; no pm clear of fwebproc on the Samsung).
- Scratch dir: spikes-floor/webproc2/. Commits GPG-signed, NO Co-Authored-By, one commit per spec item. NO pushes, PRs, GitHub comments or issues.

## Order (report to the lead at the two checkpoints, then continue unless told otherwise)
0. Emulator checks on API 30 (`lease.sh run android-emu ...`; system image android-30/google_apis exists locally — create the AVD from it, no downloads; if it can't run, report and continue on real devices). With the current wp5 APK: (a) does a page field in the embedded page get the IME on API 30? → decides remote path API 30+ vs 31+; (b) is the drawer edge swipe swallowed by the page (expected, API < 34)?
1. Indicator regression (portrait only): reproduce on Samsung/Redmi, compare fnomm vs fwebwin vs wp5 to find which change broke it, fix.
2. Geometry: drop the crop, resize only, frame by frame, coalesced to the latest size. Side-by-side recording vs fnomm of the navbar open/close (Redmi + Samsung). Fixes "can't scroll to top".
3. Focus/IME/Back per spec 1–5: two focus owners, panel never focusable, focus moves only on touch, address bar reclaims focus before showSoftInput. Verify each of the 5 points on the Samsung. Gate API 30 per step 0.
4. Edge-strip window for API < 34 (spec Q9): strip in its own small window above the browser panel/embedded page, reusing NativeSwipeHandlerHelper; tap replay to a remote page forwarded to `:browser`. Verify on the API 30 emulator; API 34+ keeps setTouchableRegion.
→ **Checkpoint 1**: message the lead (≤10 lines + APK path) so the owner can test.
5. Kill rules: QML "Browser left" vs "covered by popup" signals (replace the 2 s freeze heuristic), 30 s hidden timer constant, exceptions (pending dApp request, download, file chooser/permission dialog), same timer in background; media keep-alive via injected JS; teardown race (cancel kill on rebind, flush storage, let AMS reap).
6. Restore: saveState after every navigation (big bundles via file), clear on replay/clear-data, newest of state vs last URL, lazy per-tab, no respawn while hidden.
7. Death: onRenderProcessGone + binder death → respawn+reload once, second death within 30 s → stop (blank page, logged). Pending dApp requests rejected + popups closed on kill/death/navigation, never replayed. UI→`:browser` calls oneway, never blocking Qt/main thread.
→ **Checkpoint 2**: message the lead.
8. Activity-dependent features: check each vs fnomm on the Samsung (alert/confirm/prompt, `<select>`, autofill, file chooser, fullscreen video, camera/mic permission, target=_blank/window.open, TalkBack). Fix only what works on fnomm, via the existing QML popups where master uses them; the rest → Logged tasks.
9. Config: forward dark mode/density changes to `:browser`.
10. Memory pass (Redmi, n=3, reclaim2's harness and metric — note dumpsys TOTAL PSS already includes swap; don't add SwapPss again): post / open / closed+60 s, UI + `:browser`, vs reclaim2's B numbers. Idle CPU closed (note the ~56 % baseline confound from provider-down banners; report deltas vs post).

## Verification (lean)
- Per spec item: one device check with evidence (screenshot/recording/log line). Side-by-side recordings vs fnomm only for geometry (2) and the indicator (1). Kill/death tests on purpose: `am kill`/`kill -9` of `:browser` while visible, while hidden, with a sign popup open (cancel it — never sign/send anything); freeze via `kill -STOP` only to check the UI stays responsive (no freeze UI is built).
- Devices: Samsung = function checks (owner's personal phone: COMMON/WEBPROC safety rules — verify unlocked + our app foreground before every input batch, never PINs/biometric prompts). Redmi = memory/CPU/recordings. Lenovo = landscape/rotation only (no navbar/indicator checks). Emulator API 30 = steps 0 and 4. Redmi NEVER: app.status.mobile.debug (owner's profile). Never touch app.status.mobile / .pr on any device. Own packages only.
- If a core mechanism is blocked (e.g. focus can't be made reliable), stop and report instead of building workarounds.

## Deliverable
- Branches in both repos, one signed commit per spec item.
- Report spikes-floor/webproc2-report.md: per spec item pass/fail + evidence path; Logged tasks list (title + 1-line why); memory/CPU table; known gaps. Write `webproc2.final` next to it.
- Final message ≤10 lines.
