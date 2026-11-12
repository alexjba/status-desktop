# ship-triage: root-cause the owner's manual-test failures on spike/floor-ship (#21528)

The owner tested app.status.ffloor (spike/floor-ship tip 1e0ae314cb, worktree ~/Repos/status-desktop.worktrees/spike-floor-ship, = origin/master 518d92d76f + 52 lever commits) on the Redmi with the test profile. The checklist was spikes-floor/ship-tests.md. Findings, in the owner's words:
1. Camera startup fine (no action).
2. No community image in the navbar.
3. No profile image (owner suspects the same cause: the loopback HTTP media server may be down).
4. Opening Settings → Notifications is really slow, and so are all the other settings views. The owner can't tell whether this is a regression.
5. The first notification sound is delayed (known, accepted).
6. The Android navigation-bar indicator disappears while on the Browser tab, and doesn't come back after leaving the Browser.
7. The display flashes briefly when opening the Browser.
8. The app crashed at the end of the session. exit-info: SIGNALED status=11 (SIGSEGV), foreground, rss 258 MB, so NOT OOM. Backtrace (logcat, pid 14867, 10:37:37): QQuickWindowPrivate::polishItems → QQuickLayout::updatePolish → ensureLayoutItemsUpdated → QQuickItem::setImplicitSize → implicitWidthChanged → setImplicitWidth → (Qt6Core +0) → setImplicitWidth → jump to base.apk +0xfbc (bad pc). Full log: spikes-floor/shiptest/logcat-all.txt. HyperOS hides logcat for non-debuggable apps, so there are no QML warnings.

Goal: for each of 2, 3, 4, 6, 7, 8 find (a) whether it is a regression vs master, (b) the commit/lever that causes it, (c) the root cause in code, (d) 2–3 fix options with cost/risk/memory impact. Analysis only: no fixes committed. The output feeds a grill session with the owner.

Useful prior knowledge (scratchpad /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/):
- PATH-TO-150.md already lists, for the window-surface lever (Window.takeSurface, no HWUI buffers): "lost after Browser" and "window-surface flip back after Browser closes" as open. win-report.md has the details. Issues 6/7 likely come from it; confirm.
- Media server: ssl-report.md (plain-HTTP loopback media server, platform TLS for remote https, keycard lazy). Check cleartext policy (network_security_config / usesCleartextTraffic) for the pr variant, whether the server starts, the URLs QML gets, and whether the test profile even has a community/avatar (an empty profile may legitimately show none: compare with the base build on the same steps).
- Settings by URL: qmlslim-report.md / login-report.md (Profile subviews by URL; L6 "sections compiled on first open: +73..124 ms"). Measure first and second open per settings view, base vs ship.
- Crash: symbolize with the unstripped libs from the ship build (worktree build dirs) and the matching Qt kit (e2) debug info; llvm-symbolizer from the NDK. Look at the by-URL / DeferredComponent / Loader changes (items destroyed while a Layout polishes, a connection to a freed object) and the StatusSectionLayout single-orientation change. Reproduce with a debuggable build of the same branch.

## How
- New worktree is not needed; build from spike-floor-ship, but do NOT commit there (analysis only; a scratch branch spike/floor-triage off it is fine for diagnostics).
- Builds: (A) baseline = origin/master 518d92d76f with the same flags as the ship build (pr release, e2 kit; order files irrelevant), package app.status.fbase; (B) ship debuggable variant (DEBUG=1 release-like, so logs and run-as work), package app.status.ffloordbg. Use mem-r1's scripts as a start: spikes-floor/mem1/sbuild.sh, run-sbuild.sh, abuild.sh, and spikes-floor/ship/. Fresh-worktree gotchas: memory reference_fresh_worktree_mobile_build_gotchas.md.
- For bisecting a lever, revert single commits on the scratch branch rather than building many full stacks; use the Lenovo for quick repros.
- Build lease, device leases, foreground waits on .done files, Redmi jobs ≤25 min, own packages only, never touch app.status.mobile/.debug/.pr or app.status.ffloor (the owner's test install), don't kill the adb server, <test password> on profiles you create only: COMMON.md and MEM1.md "Rules".
- Report: spikes-floor/triage-report.md (WIP until done). Per issue: regression yes/no, culprit commit, root cause (file:line), evidence (logs, screenshots, timings), fix options. ≤10-line message to the lead when done; blocked → stop and report.
