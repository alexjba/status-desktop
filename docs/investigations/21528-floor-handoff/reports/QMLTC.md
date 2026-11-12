# Spike s-qmltc: does the QML type compiler (qmltc) cut the FIXED floor? (#21528 / #22657)

Read first, in /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/: COMMON.md (rules: devices, packages, profile setup, waiting on jobs), WAVE3.md (goal, metric, device caps, deliverable format), PATH-TO-150.md, final-report.md (current floor and how it was measured). Also memory file /Users/alexjbanca/.claude/projects/-Users-alexjbanca-Repos-status-desktop/memory/project_qmltc_findings.md (April 2026 attempt: qmltc refuses composite types from other modules; 831/947 files failed).

## Question
Today type data (compilation units, QQmlTypeData, property caches, metaobjects) is built on the heap when a parent file compiles and cached for the engine's lifetime. qmltc generates C++ classes with static metaobjects instead. Does that move memory off the heap enough to lower TOTAL PSS+SwapPss at the login screen AND after login, after paying for the bigger .so (code + dirty .data.rel.ro)? Answer with numbers, not theory.

## Base
`spike/floor-final` (tip 27d06db05e, 152.7 login / 244.9 post). Worktree: `git -C ~/Repos/status-desktop worktree add ~/Repos/status-desktop.worktrees/spike-floor-qmltc -b spike/floor-qmltc spike/floor-final`, then submodules. A fresh worktree does not build out of the box: read reference_fresh_worktree_mobile_build_gotchas.md, and copy libstatus.so, libsds.so, libcrypto_3.so, libssl_3.so and mobile/build/android/qt6/openssl-android-arm64 from ~/Repos/status-desktop.worktrees/spike-floor-final (same paths).
Build harness: copy spikes-floor/final/{rbuild.sh,chain-final.sh,m.sh,job.sh} and see how w-final built `final` (KIT=e2 + order-file env). Change W=, D=, BUNDLE_IDENTIFIER=app.status.fqmltcr. Build the base and your branch with the same script/flags/package.

## Plan (stop at each gate if the answer is already clear)
1. **Feasibility, Mac, ≤3 h.** Pick the slice with the most type data that qmltc can actually compile. Candidates: (a) the login/onboarding closure, created from C++ via the generated class (qmltc only helps where the root is instantiated from C++ — verify this in Qt 6.11 source/docs); (b) StatusQ leaf types inside one module. A host-only patch of qmltc (the cross-module check at `tools/qmltc/qmltcvisitor.cpp` ~786, source in ~/Qt/6.11.0/Src/qtdeclarative) is allowed for the spike; build it into your own dir, don't overwrite ~/Qt/6.11.0/macos/bin/qmltc. Record: files compiled / failed (with reasons), generated C++ lines, .so size delta (.text, .data.rel.ro, relocation count via llvm-readelf).
2. **Mac heap probe (fast).** ~/Repos/research-qmlmeta (see qmlmeta-report.md) or a desktop build: heap at the login screen base vs qmltc. If the heap saving is under ~1 MB and the .so grows more than that, stop and report.
3. **Device A/B.** Lenovo first for smoke/exploration. Redmi release A/B n=3 alternating (≤25-min jobs, scripted, `.done` + Monitor): login 37 s, post +120 s, plus native/.apk/.so/EGL. Also am-start TotalTime, ENTER→wallet, idle CPU 60 s. Functional: login works, onboarding screens render, rotation.

## Rules
- Don't build in ~/Repos/status-desktop.worktrees/spike-floor-final or spike-floor-r3 (shared). Builds only through the buildbox lease, devices only through lease.sh (caps in WAVE3.md). Own package only.
- Signed commits on spike/floor-qmltc, no Co-Authored-By, no push/PR/GitHub comments. If you spawn sub-agents, pass `run_in_background: true` (boolean, not the string).
- Report: spikes-floor/qmltc-report.md, marked WIP until done. Final message ≤10 lines + report path. Blocked (permission denied, build broken >30 min) → stop and report; never work around a denial or ask another agent to do it.
