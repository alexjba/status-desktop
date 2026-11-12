# mem-r1: first memory round on replay builds (#21528 / #22657)

Goal: the remaining 2.7 MB at the login screen (spike/floor-final = 152.7 login / 244.9 post, Redmi, release). Target ≤150 real at login, and each saving must persist after login (fixed floor). Login-only savings and accounting tricks don't count. No CPU regressions.

## Read first (scratchpad = /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/)
- COMMON.md (device rules, packages, leases, waiting on jobs), PATH-TO-150.md, final-report.md (how 152.7 was measured).
- qmlmeta-report.md: 437 never-instantiated types (9.9 MB Mac); the boundary-root table (top 35 = 4.5 MB, `roots.py`, deps-N.tsv); "Dropped": loading HOT chat/community/home types by URL cost >30 ms first opens — don't redo those.
- replay-report.md + ~/Repos/status-desktop.worktrees/replay-mode/docs/replay-mode.md: the replay tooling, its make targets, the probe (STATUS_REPLAY_PROBE_AT: types loaded/instantiated, malloc, smaps), the calibration (real ≈ replay + Nim heap ~4 login / 5–6 wallet + Java stub 2.7 + Nim code pages; release TOTAL sd 10–24 MB → rank on native heap, probe malloc and type counts).
- Memory: reference_qml_type_closure_probe.md, feedback_agent_missed_bg_notices.md (in /Users/alexjbanca/.claude/projects/-Users-alexjbanca-Repos-status-desktop/memory/).

## Workspace
- New branch spike/floor-replay = spike/floor-final (27d06db05e) + the replay tooling from feat/replay-mode (83b633eccc, 32 commits, based on newer master). Worktree ~/Repos/status-desktop.worktrees/spike-floor-replay. Port by cherry-pick (or a single squashed port commit if conflicts make that cleaner); the seaqt hooks pin is the local branch replay-hooks @1447e1e in vendor/nimqml-seaqt — it must be reachable from your worktree's submodule. Don't modify feat/replay-mode or spike/floor-final.
- Fresh-worktree gotchas + libs to copy from spike-floor-final: see QMLTC.md "Base" paragraph. Build harness: spikes-floor/final/{rbuild.sh,chain-final.sh,m.sh,job.sh} (KIT=e2 + order files) for the real app; replay/ scripts (apk-release.sh, calr-*.sh) for replay packages. Use your own packages: app.status.fmemr (real, pr release), app.status.fmemrp (replay, release).
- Never touch the main clone's working tree (`git -C`), no bare `git stash`. Signed commits, NO Co-Authored-By, NO push/PR/GitHub comments.

## Plan (gates)
1. **Port + baseline.** Build the port on desktop; `make run-replay-tests` passes. Record a desktop recording with `make record-run` and an Android one on the Lenovo/Redmi with `make mobile-record-run MOBILE_REPLAY_PROFILE=release` (test profile only, unattended). Mac probe on desktop replay at login and wallet = baseline types/malloc. Report the Redmi replay baseline (release, n=3) at login and wallet.
2. **Iterate on Mac replay (fast loop).** Re-run roots.py on the current branch's type graph (floor-final already converted some roots). Convert the largest never-instantiated boundary roots that are cold (popups, rare views, settings subpages, onboarding/keycard flows not on the login path), s-qml/floor-login pattern (load by URL / Loader on demand). One commit per lever, message states the measured delta. Per lever: loaded types and probe malloc at login and wallet, replay A/B on Mac. Also allowed: other QML-side fixed-floor items you find with the probe (e.g. the CJK font probe at login, eager Qt6Nfc/Multimedia core) if replay can measure them.
3. **Redmi replay A/B** of the stack (release, n≥3 alternating, ≤25-min jobs): native, probe malloc, TOTAL PSS+SwapPss, types.
4. **One real-app Redmi A/B** of the winners vs spike/floor-final (pr release, same harness as final-report, n≥5 alternating): login + post (+120 s). Report whether ≤150 is reached.
CPU per lever: first open of each converted root on the Redmi (reject >30 ms regressions), login→wallet, idle CPU 60 s (real app, step 4). Functional: open every converted popup/view once through the UI (replay or real), screenshot.

## Rules
- Builds only through the buildbox lease; Lenovo first for smoke; Redmi only for numbers, whole job scripted inside `lease.sh run redmi ... -- bash job.sh; echo $? > job.done`, ≤25 min per job.
- Waiting: wait in the FOREGROUND with Bash `until [ -f X.done ]; do sleep 10; done` (timeout 540000), repeat until it exists. Never end your turn while a job runs. Background notices and Monitor events don't reliably wake you.
- Device rules in COMMON.md; NEVER touch app.status.mobile, app.status.mobile.debug, app.status.mobile.pr. Test password <test password> only, profiles you created only. Don't kill the adb server. No system-permission changes. Don't edit the shared lease.sh.
- Recordings stay local (gitignored), never committed.
- If you spawn sub-agents: `run_in_background: true` (boolean).
- Report: spikes-floor/mem1-report.md, marked WIP until done; per lever MB (login/post, Mac + Redmi), CPU, functional check, risk. Messages to the lead ≤10 lines. Blocked (permission denied, build broken >30 min) → stop and report.
