# Handoff: Android UI memory floor (#21528 / epic #22657): pivot from Browser-out-of-process back to the other perf levers


Lives on fork branch `docs/21528-floor-handoff` at `docs/investigations/21528-floor-handoff/`. All `reports/...` paths are relative to this file.
Date: 2026-11-12. Owner: alexjba. Repo: status-app (upstream `status-im/status-app`, personal fork `alexjba/status-desktop`).

## Next session's focus
1. The branches are in the fork (`alexjba/status-desktop`) except one; see "Push status".
2. **Extract the work into separate, reviewable branches**: one lever per branch off fresh `origin/master`, so each can become its own PR. Source material: `reports/PATH-TO-150.md` ("What's in it" table: lever → branch → measured delta) and `reports/final-report.md`.
3. **Go back to the other perf improvements** (the non-Browser levers). The Browser-out-of-process work is **parked**; don't continue it unless the owner asks.

## Push status
- Remote `fork` → `https://github.com/alexjba/status-desktop.git` is set up in `~/Repos/status-desktop`.
- **61 of the 62 branches are pushed** to the fork under the same names (list: `reports/pushrefs.txt`). GitHub rejects a bulk push of all of them; push one at a time.
- **Not pushed: `spike/22677-url-popups`.** It commits `docs/investigations/21528-memory/profiles/spikes/22677/hold-send-notrim-malloc_history.txt` (106.8 MB, over GitHub's 100 MB limit). It's still local. To push it, rewrite the branch without that profile dump (owner's call).
- Don't push to upstream. Fork PRs target the fork's `master` (see CLAUDE.md "Branch Policy"); upstreaming is done by the owner.

## Branch map (status-app, local in `~/Repos/status-desktop`; worktrees under `~/Repos/status-desktop.worktrees/spike-floor-*`)
Tip SHAs: `git for-each-ref refs/heads | grep -E 'floor|21528|measure/|22677'`. Measured deltas per lever: `reports/PATH-TO-150.md`.

**Final stack (the main result: login 189.6 → 152.7 MB, post-login −42)**
- `spike/floor-final` (27d06db05e): 21 levers stacked on `spike/floor-r3-base` (e1f906462e). Report: `reports/final-report.md`.
- `perf/21528-android-ui-memory-floor` (51c123c2cf): **upstream draft PR status-im/status-app#23106**, the in-process shipping candidate. Reference build on the devices is `app.status.fnomm`.
- `spike/floor-ship`, `spike/floor-ship-masterlike`: ship-test variants (`reports/ship-tests.md`).

**Per-lever branches (extraction candidates, one PR each)**
- Build/link + QML URL loading + qrc stub + no atlas: `spike/floor-stack-nodefer` (base `spike/floor-stack`, `spike/floor-stack-base`). Parts: `spike/floor-build` (-Oz libStatus), `spike/floor-qtlibs` / `spike/floor-gfx` (no texture atlas), `spike/floor-pages`.
- QML slim + type closure: `spike/floor-qmlslim`, `spike/floor-qmlmeta`, `spike/floor-r3-base`.
- Custom Qt kit + hot-first order files: `spike/floor-order` (probe: `spike/floor-order-probe`, not for merge). Order files live in `~/Qt-custom/order-files/`.
- Rare popups/views by URL, keycard drawer on demand: `spike/floor-login`, `spike/22677-url-popups`, `measure/lazy-popups`.
- Cheap native wins (MIME by suffix, bundled ▴▾ font, dialogs on first open): `spike/floor-cheap`. The WIP/diag variants are not for merge: `-wip`, `-wip2`, `-wip4`, `-mimeonly`, `-diag2`, `-diag6`.
- Media stack on first use + SoundPool: `spike/floor-media`. Native CameraX QR: `spike/floor-qr`. Drop QtMultimedia/FFmpeg: `spike/floor-no-qtmultimedia`. QtMultimedia imported only on use: `spike/floor-replay`. Hook branches (`-hook`, `-abch`, `-baseh`, `-basehook`) are test-only.
- No HWUI buffers behind Qt's SurfaceView: `spike/floor-win` (diag: `spike/floor-rca-surface`, `reports/rca-surface-report.md`).
- No OpenSSL in the UI process: `spike/floor-ssl` (tip is a diagnostic commit to drop).
- Precompile timing: `spike/floor-defer`, `spike/floor-defer-appmain` (dropped as levers, see the Dropped list).
- Docker Qt/FFmpeg build fix: `spike/floor-qt-docker`.

**Measurement / probe only (never merge)**
- `spike/floor-*-mark`, `spike/floor-qml`, `spike/floor-plat`, `spike/floor-code`, `spike/floor-fault`, `spike/floor-login-probe`, `spike/floor-replay-diag*`, `spike/floor-build-edit`.
- `measure/floor-inventory`, `measure/qml-inventory` (the QML inventory tool), `measure/login-profile`, `measure/native-purge`, `measure/22655`, `perf/21528-memory-footprint` (older; includes the `lease.sh` tooling).

**Browser work (PARKED)**
- `spike/floor-webwin` (5ac46af23f): WebView in its own window. Report: `reports/webwin-report.md`.
- `spike/floor-reclaim2` (b6cf462780): in-process vs out-of-process close measurement. Result: in-process destroy returns 21 of 111 MB; out of process returns 117 of 131. Report: `reports/reclaim2-report.md`.
- `spike/floor-webproc` (4cb40e0e56): out-of-process v1 spike.
- `spike/floor-webproc2` (63056c0797): v2 master-parity app side. Uncommitted build glue in that worktree (`build.gradle`, `buildStatusQ.sh`) points at the local mobilewebview clone; it's not real code.

## mobilewebview (separate repo `status-im/mobilewebview`; no fork exists under alexjba)
- Clone: `~/Repos/status-desktop.worktrees/mobilewebview-webproc` (remote `github` = upstream, `origin` = the webwin clone). Also `~/Repos/status-desktop.worktrees/mobilewebview-webwin`.
- Draft PR status-im/mobilewebview#34, branch `ab/browser-process` @ a4e0050 (v1 spike shape). Body: `reports/mwv-pr-body.md`.
- Local branches:
  - `spike/webview-process-v2` @ 9926f09: master parity, no option A, JS-dialog commit dropped.
  - `spike/webview-process-v2-optA` @ 7b44b54: option A, keeps the JS-dialog forwarding as 9356a8a.
  - `spike/webview-process-v2-focus` @ 47dcf52: the option A focus part only (EmbeddedSurfaceHost, panel removed), WIP, never built or tested. Committed by the lead after webproc2 was stopped.
  - `spike/webview-process` (v1, includes the dropped df2843a); `spike/webview-window` @ 97455aa (webwin).
- Don't push anything to status-im/mobilewebview without the owner's OK.

### Browser state at pivot (for whoever resumes it)
Spec: `reports/WEBPROC2-decisions.md`. Brief: `reports/WEBPROC2.md`. Results and logged tasks: `reports/webproc2-report.md` (it has a "State at pivot" section if webproc2 finished it). Adversary review: `reports/webproc-adversary.md`.

Open issues found by the owner's hand test on v11 (Samsung S21 FE, API 36):
- Page-field keyboard fails without option A (`windowFocus=false`, "Ignoring showSoftInput"). Passes on the Redmi (API 35) and with v10 (option A). Fix: the option A focus part only.
- `<select>` doesn't open out of process (v10 and v11). It works on fnomm, so it's a parity break.
- Hiding the address-bar keyboard needs 2 taps (v10 and v11). Not yet compared against fnomm.
- Not tested yet: dApp sign request, Lenovo rotation, TalkBack.
- Master has no JS alert/confirm/prompt (logged task, not parity).
- Measurement pitfall: scripted `adb input tap` doesn't raise the embedded IME, and `dumpsys` gives false negatives. Use the owner's hand tests or ImeTracker logcat lines.

## Key reports (in this directory, copied out of the session scratchpad)
All in `reports/ (next to this file)`:
- `COMMON.md`: shared agent brief (workspace, build lease, device rules, test-profile setup, waiting on long jobs).
- `PATH-TO-150.md`, `final-report.md`: outcome, levers, "To reach 150", "Must verify before shipping", "Dropped".
- `triage-report.md`, `mem1-report.md`, `nomm-report.md`, `qmltc-report.md`, `replay-report.md`, `rca-media-report.md`, `rca-surface-report.md`, `ship-tests.md`: per-spike results.
- `smapdiff.py`: per-mapping smaps diff tool.
- The APKs and raw data were not copied. They are in the session scratchpad `spikes-floor/*/` until it's wiped.

## Rules that carry over (from memory and the owner)
- Memory index: `~/.claude/projects/-Users-alexjbanca-Repos-status-desktop/memory/MEMORY.md`. Most relevant: `project_21528_android_memory_footprint.md`, `project_21528_browser_chromium_cost.md`, `feedback_master_parity_minimal_scope.md`, `feedback_lean_verification.md`, `feedback_no_cpu_regressions_low_end.md`, `feedback_epic_prs_upstream_not_fork.md` (check it before choosing the PR target).
- Builds always go through the buildbox lease; devices through `lease.sh` (`~/Repos/status-desktop.worktrees/perf-21528-memory/mobile/scripts/profiling/lease.sh`). Don't kill the adb server or edit lease.sh.
- Devices:
  - Redmi A5 = memory/CPU.
  - Samsung = the owner's personal phone, function checks only. Unlocked and our app in the foreground before each input batch. Never touch the "Sniveling Profu…" profile or the system-update dialog. No `pm clear` of fwebproc there.
  - Lenovo = landscape.
- Packages: only our own `app.status.f*`. Never install over, clear, log into or launch `app.status.mobile`, `.debug`, `.pr`. On the Redmi, `.debug` holds the owner's profile.
- Never enter lock-screen PINs. Never interact with biometric prompts. Use the throwaway test password only on our own test profiles (see COMMON.md; don't copy it elsewhere).
- Commits are GPG-signed with no Co-Authored-By trailer. No pushes, PRs, GitHub comments or issues without the owner's go-ahead. Concise reports. No CPU regressions on low-end devices.
- Agents: exit files plus Monitor, a lead watchdog, lean verification, one commit per lever.

## Live agents
- `webproc2` is stopped. No leases held (all free at pivot).
- Devices at pivot:
  - Samsung: fwebproc v10.
  - Redmi: fwebproc v11.
  - Lenovo: fwebproc v11.

## Suggested skills
- `request-refactor-plan` or `superpowers:writing-plans`: plan the split of `spike/floor-final` into one-lever branches/PRs.
- `superpowers:using-git-worktrees`: one worktree per extracted lever, from fresh `origin/master`.
- `resolving-merge-conflicts`: cherry-picking levers onto current master will conflict.
- `mobile-build` / `mobile-deploy` / `mobile-control` / `mobile-logs`: per-lever device checks.
- `diagnosing-bugs`: for any regression found while re-measuring a lever.
- `code-review`: before each extracted branch becomes a PR.
- `grilling`: before spawning agents for the next perf lever (owner rule: grill before spawn).
- `superpowers:verification-before-completion`: measured numbers before any claim.
