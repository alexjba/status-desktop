# Brief: Browser close memory, in-process vs out-of-process (agent "reclaim2")

Read first, in full: COMMON.md (same dir); it applies except where this brief overrides it. Then webwin-report.md section "Reclaim after close" (your predecessor's experiment and its flaw), and webproc/ (the out-of-process spike's scripts: m.sh-style harness, summ.py, smapcat.py, redrun.sh).

## Decision this feeds (owner)
Keep the Browser in-process and destroy its WebViews on close, or move the Browser to a separate process? Need: how much memory comes back after closing the Browser, in both designs, measured the same way.

## Builds (Redmi A5, debug, same flags as webproc's builds)
- **A: in-process, as master** = PR tip 51c123c2cf (the main window gets HWUI back once a WebView shows and keeps it; that is intended, do NOT use the webwin/webproc surface changes) + the reclaim debug hooks from 5ac46af23f (cherry-pick; resolve trivially). New worktree `~/Repos/status-desktop.worktrees/spike-floor-reclaim2`, branch `spike/floor-reclaim2` from 51c123c2cf. Stock mobilewebview pin (no FETCHCONTENT override). Package `app.status.freclaim`.
- **B: out-of-process** = existing APK `webproc/apk/wp5.apk` (package `app.status.fwebproc`), unchanged. Leaving the Browser kills `:browser` within ~5 s.
- Build A via the buildbox lease as COMMON says.

## Measurement (Redmi, n=3 runs per build, medians; report every run too)
Per run, fresh launch → login with the test profile → wallet. Make sure the post-login baseline has NOT restored the Browser section (the predecessor's runs 2–3 did, invalidating them): verify no WebView/Chromium mappings in the UI process and no `:browser` process at the "post" snapshot; if the app restores the last section, navigate away from the Browser before force-stop or reset that setting.
Snapshots (dumpsys meminfo TOTAL PSS, native, .apk mmap, .so, EGL, GPU, Unknown; smaps where readable):
1. post: login +120 s, Browser never opened.
2. open: Browser opened, the same real URL loaded in one tab (pick a light static page, e.g. https://example.com, same for A and B), +30 s.
3. A only: closed-hidden: leave the Browser to wallet (master behaviour, WebViews kept), +60 s.
4. A only: closed-destroyed: debug hook step 1 (unload Browser + gc + trimComponentCache), +60 s. Then steps 2–4 cumulatively, snapshot after each.
5. B: closed: leave the Browser to wallet, +60 s; confirm `:browser` is gone.
For every snapshot report UI process AND `:browser` (B) and the device-side sum.
Also: idle CPU % over 60 s at "closed" states (UI + :browser), and Browser reopen time after close (tap → first page painted) for A-destroyed vs B (cold :browser).

## Deliverable
- Table: post / open / closed (each variant) per build, deltas vs post, i.e. "MB not returned after close". Plain conclusion: in-process destroy returns X of Y MB; out-of-process returns X of Y MB.
- Report `spikes-floor/reclaim2-report.md` + `reclaim2.final`. Commit A's branch (GPG-signed, no Co-Authored-By). No pushes/PRs/comments.
- Final message ≤10 lines.

## Device notes
- The owner may still hold the redmi lease for manual testing; queue on it (LEASE_WAIT_MAX large), build A meanwhile.
- Redmi only. Own packages only (`app.status.freclaim`, `app.status.fwebproc`). NEVER install over, uninstall, clear, log into or launch app.status.mobile / .debug / .pr. If fwebproc on the Redmi has no usable test profile of ours, `pm clear app.status.fwebproc` is allowed on the Redmi only, then create the test profile per COMMON.
- Don't kill the adb server, don't edit lease.sh. Redmi adb serial: adb-9b010059305330363400f1f217fa8c-Dc9BQB._adb-tls-connect._tcp (lease prints a shorter SERIAL; use this one).
- If the core measurement is blocked, stop and report.
