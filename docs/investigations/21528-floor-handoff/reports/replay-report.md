# impl-replay report

Worktree ~/Repos/status-desktop.worktrees/replay-mode, branch feat/replay-mode (upstream master 4553a65ccd). 32 signed commits, not pushed; tip 83b633eccc. nimqml-seaqt: local branch replay-hooks 1447e1e (pinned); earlier pins kept as replay-hooks-v0 / -v1.

**Status: DONE, final re-review APPROVED (pending owner go for seaqt PR + upstreaming).** Final summary at the end.

## Phase 0: done
docs/replay-mode.md + ADR 0011 (owner decisions applied).

## Phase 1: done (desktop, macOS, Qt 6.11.1)
- `make record-run`: unattended on a clean data dir. It creates a test profile through scripted onboarding (<test password>, Save password off when offered), then records login screen -> login -> wallet, then quits. Fails if the startup/loggedIn/exit moments are missing. Verified end to end from an empty Status/record-profile.
- `make replay-run RECORDING=.. [REPLAY_MOMENT=..] [REPLAY_MODE=realtime|step|frozen]`: no Nim, no status-go.
  - startup (login screen): pixel-identical to the recorded screenshot.
  - wallet: same layout and data as recorded, banner included (after the fix below).
  - realtime from the beginning (no moment): login screen -> login -> wallet walks by itself; final frame identical to the recorded wallet screenshot.
- Banner miss, explained and fixed (4a7cb8801d): the banner shows when `privacyModule.mnemonicBackedUp` is false. The recorder redacted it by name ("mnemonic") and wrote "$redacted". Replay decoded that string as bool `true`, so the banner was hidden. The call log could not flag it because it is a property, not a slot. Fix: redact only text values under sensitive names (secrets are strings); test sensitiveNamesOnlyHideText.
- Asset-row miss, fixed (realtime run): replayed QML called getCurrencyAmount(0,"dai") before the timeline reached the recorded call. The fallback took the latest call with other args, so every row said USDC. Exact args now win over position. Remaining unmatched calls in a full realtime walk: getWCActiveSessions(now), whose args depend on the clock, and one void cleanup call.
- Recording size (fresh profile, ~1 min): 2.6 MB, 117 schemas, 1668 events. Gaps: only the camera/QR-scan context properties, which are set via the C++ shim and picked up by enumeration.
- Normal run: 0 libStatusReplay pages mapped (lsof/vmmap on a running app), hooks nil.
- Tests (make run-replay-tests): RoundTrip 15 cases, QmlConsumer, ScriptRunner, SetupParity all pass with no warnings. Nim: ttracehooks (seaqt) and replay_recorder_test pass.
- Design changes made while testing on the real app:
  1. Read-driven values: the recorder never calls getters QML did not call (an eager read crashed Nim).
  2. Metacalls on deleted or unknown objects are ignored (crash: a slot deleted its own object).
  3. Replay plays up to post-load moments, because QML keeps its own page state.
  4. Late reads apply from their segment start; forward object refs are built early.
  5. Scripted clicks become `ui` events that replay performs again.
- Open:
  - `make nim-test-run` on macOS can't find libstatus.dylib at runtime (pre-existing; I ran the test binary directly).
  - Qt 6.11.0 desktop build is broken on master (generated Qt6Multimedia.pc lacks QtGui), so I used 6.11.1.
  - Wallet banner gap.

## Phase 1 review batch: done (commits 3836b0202a, 690853a78f, 6bfd389026)
- seaqt pin f2caa4f: test binaries dropped and gitignored, model data hook added. Pin is still local-only, which blocks merging upstream; noted in the doc.
- UAF: schemas held by unique_ptr. Red seen under ASan (heap-use-after-free); all tests pass under ASan.
- Redaction: signal params, role names, credential|connectionString. Test covers requestStoreCredentialToKeychain, keycardAuthSuccess, stayLoggedInSaveRequested, the connectionString getter, validateConnectionString, and a seedPhrase role.
- CppOwnership on replay objects, with a gc() test.
- Fingerprint records the real values (incubation 12/300/2, cache size 536870912, Nim QML import paths), no typeid. `status-replay --check-setup` runs after record-run and fails on any diff. Verified: "replay setup matches the recorded app".
- Compile gate: off in production unless -d:statusRecorder.
- Roles are read-driven: model data hook in seaqt, `col` events, replay lookahead when QML reads a role earlier than recorded. Test asserts unread roles are never read.
- CI: replay_recorder_test uses a plain-C fake lib (test/nim/fake_statusreplay.c), so tests-nim no longer depends on libStatusReplay or Qt private headers.
- Suggestions done: bounds checks with divergence warnings; deleted objects never outlive their metaobjects (ASan test); quint64 above INT64_MAX; absolute default record path; single notify on write; only objects Nim handed out are followed.
- Phase 2 item 1: Player moved into the lib, PlayerTest covers realtime, speed, pause, step and fast-forward.
- Regression found by re-recording and fixed (test added): an equal-value notify must still replay when QML's re-read was recorded first. The wallet header had shown "Account 1".
- Desktop recheck: wallet replay matches the recording screenshot again.
- Tests: make run-replay-tests passes all 6 (RoundTrip 19, Player 6, RecorderEdges 10, QmlConsumer, ScriptRunner, SetupParity), and the same tests pass under ASan. ttracehooks passes, with red verified for the data hook. replay_recorder_test passes when the binary is run directly; the make runner on macOS still lacks DYLD.

### CI proposal (no Jenkinsfile edits)
Separate stage `replay-tests` on the Linux executor:
1. `make statusq`
2. `make run-replay-tests` (QT_QPA_PLATFORM=offscreen, about 10 s)
3. Optional nightly ASan variant: configure ui/replay with -fsanitize=address.

Keep it independent of tests-nim-linux. It needs Qt private headers (CorePrivate, QmlPrivate) on the executor; the Qt kit has them.

## Phase 2: what is left
Covered: realtime walk pre-login -> login -> post-login, verified by screenshot; play-up-to-moment; scripted UI actions replayed.
Left:
1. Done in the review batch (Player in lib + PlayerTest).
2. Step mode (F8/F9) and pause (F10) were never exercised on the real app.
3. Manual UI input is not recorded, only scripted clicks. A recording driven by hand (RECORD_SCRIPT=none) replays the Nim state, but QML-only navigation diverges.
4. Realtime timing for large recordings: there is no back-pressure if QML is slower than the recording (the fast-forward path does have it).

## Phase 3 (Android): done (4db0060749, low items 384d547006)
- Replay variant (BUILD_VARIANT=replay, package app.status.mobile.replay; mine app.status.freplay.debug): no libnim, libstatus, stub or service; StatusQtActivity skips the stub.
  - Lenovo: replays the desktop recording (wallet).
  - Lenovo and Redmi: replay the Android recording lenovo-2. Login screen and wallet render; phone layout on the Redmi.
- Unattended Android recording, verified on the Lenovo (package app.status.frecord.debug): the RECORDER=1 build creates its own profile through scripted onboarding (Save password and biometrics off), logs in, opens the wallet, and writes startup/loggedIn/loggedIn-settled/wallet/exit. Recording is 2.9 MB, 1412 events. Gaps are only the 2 shim context properties.
- Make targets (dry-run checked): mobile-record-run, mobile-replay-run.
- Fixes found on device:
  - files adb creates in a dir that adb created are unreadable by the app (EACCES), so the scripts start the app once first;
  - Android ends the process without aboutToQuit, so scripted quits write the exit moment and the call log before quitting;
  - stale main.o across variants;
  - env option name for --quit-after.
- Known difference: the "Enable notifications" prompt shows in replay but not in the recording. It is driven by QML-persisted settings (enablePushNotificationsFreshInstallSeen) and OS permission state, which are not recorded. Documented.
- Expected cross-platform setup differences are printed at replay start (DPR, font, graphics API, QSG_ATLAS env).
- Left: phase 3b (media capture through the NAM factory), phase 4 validation.

## Phase 3b (media): done (64a0906e6d)
StatusQ reply tap + request interceptor. The recorder keeps every successful GET body in media/ (peeked at, so QML still reads it all); local media-server URLs are keyed without host and port. Replay serves those and lets the rest go to the network. MediaTest passes. Desktop run: 6 responses recorded (token icons). A fresh profile has no media-server images, so the local path is covered only by the key test.

## Phase 4 (validation): done
Mac, qmlmeta probe (DYLD_INSERT), same recording (scripted run B), malloc MB:
| moment | real, plain | real, recording | replay |
|---|---|---|---|
| login screen @20s | 69.9 (1252 loaded / 121 inst) | 70.1 (1252 / 121) | 68.6 (1252 / 121) |
| wallet settled | n/a* | 117.9 (1254 / 324) | 101.4 (1252 / 316) |
- The instantiated composite-type sets are identical by URL at both moments; header counts differ by execution units only.
- First measurement showed replay +40 MB malloc. Cause: every timeline line kept readLine's 32 KB buffer (1318 × 32 KB). Fixed in this commit; now within 1.3 MB of real at login.
- *Real after-login needs the scripted login, which needs the recorder (record mode). Recorder overhead at the login screen is +0.25 MB on Mac and +1 MB native on the Redmi. After login it is larger: the registry and read values grow, part of the 16.5 MB gap on Mac.

Redmi A5, UI process, dumpsys meminfo (MB):
| moment | run | native heap | TOTAL PSS | swap PSS |
|---|---|---|---|---|
| login screen | real, no recorder (n=2) | 86.9 / 86.8 | 250.7 / 254.2 | 0.8 |
| login screen | replay (n=3, median) | 83.9 | 254.4 | 0.8 |
| wallet +60s | real with recorder (n=1) | 136.4 | 321.8 | 7.2 |
| wallet +40s | replay (n=3, median) | 131.5 | 329.3 | 0.8 |
- Login screen: replay native is 3 MB below real, and TOTAL PSS matches within the ±15 MB noise. Replay is usable as a stand-in for the login-screen floor.
- Wallet: native is 5 MB below a recorded real run (the recorder's own allocations); PSS is within noise. Swap differs (7 vs 0.8): the real app has the status-go service and Nim, and the replay never swaps.
- Replay omits Nim and status-go entirely. The real UI process's native heap includes Nim allocations, so replay understates the real floor by whatever Nim allocates in the UI process. Here that is 3 to 5 MB.

## Open
- seaqt pin 1447e1e is local-only; it blocks merging upstream until the seaqt PR lands (owner's go).
- Phase 2 leftovers: step/pause keys were never tried on the real app; no back-pressure in realtime mode.
- QML-persisted settings and OS permission state are not recorded (notifications prompt on Android).
- `make nim-test-run` on macOS lacks DYLD for libstatus (pre-existing).
- The CI stage is proposed, not wired.

## Phase 4 redo, first pass (SUPERSEDED by the clean calibration below: debug packages, real side recorded)
Changes first:
- 36e4bb8fb3 media: same-size placeholders by default (status, type, size, pixel size, format, decoded pixel format, frame count; a solid GIF for animations, an empty SVG for SVGs, filler for non-images). STATUS_RECORD_MEDIA=bytes is opt-in, and the doc warns such recordings hold personal images. Tests: placeholders decode to the same size, format and frames; default recordings contain no bodies.
- 4a76df4e0e QML settings: the default QSettings are snapshotted at record time and restored into replay's own ini store. Scripts answer the notifications prompt with "Don't ask me again" then "Maybe later" in every run. The in-process probe STATUS_REPLAY_PROBE_AT runs in both the recorded app and replay.

Setup:
- Redmi A5, UI process only.
- Both packages are DEBUG=1 builds, which are release-like: Nim -d:release -d:useMalloc, Qt CONFIG+=release, CMake Release; only the Gradle debug type, which makes them debuggable for smaps.
- Real = recorder build with scripted login; each of the 3 real runs records itself.
- Replay = realtime replay of real run 3's recording.
- Measured at login screen (+25 s) and wallet (+160 s, about 120 s after the wallet click), n=3.
- Run 1 of each side was taken right after install and had heavy swap (59/33 MB SwapPss), so it is reported but excluded; medians are of runs 2 and 3.

| | real native | replay native | Δ | real TOTAL PSS | replay TOTAL PSS | Δ | SwapPss r/rp |
|---|---|---|---|---|---|---|---|
| login | 87.5 | 83.6 | −3.9 (−4.5%) | 262.7 | 243.0 | −19.7 (−7.5%) | 1.0/0.8 |
| wallet | 138.1 | 129.2 | −8.9 (−6.5%) | 329.1 | 301.5 | −27.6 (−8.4%) | 0.5/1.0 |

QML types (probe):
- Login: identical. 1239 loaded, 115 instantiated, same URLs.
- Wallet: same 1241 loaded. Instantiated 332 real vs 327 replay. The 5 extra in the real run are the login splash (SplashScreen, DidYouKnowSplashScreen, DidYouKnowMessages, StatusProgressBar, Pane): the real app shows them while status-go logs in and keeps them instantiated. Replay logs in instantly from the timeline and never shows the splash.
- The notifications prompt is in neither run now.

Probe malloc (mallinfo): login real 91.4/92.8 vs replay 88.2–88.7 MB; wallet 142.2 vs 133.9 MB.

Per-library PSS (smaps, run 3), what differs:
- base.apk: −8.0 MB at login, −10.3 MB at wallet. Native libraries are mapped straight from the APK, and the replay APK has no libnim_status_client, libstatus_stub or libstatus-keycard-qt, so their code and data pages are absent.
- [anon:malloc]: −4.0 at login, −9.4 at wallet. This is Nim's heap (with -d:useMalloc it lives in malloc) plus the recorder's own data.
- libGLES_mali: −1.9 at both moments; the replay builds slightly fewer GL resources. The splash may account for some of it; not investigated further.
- Everything else (fonts, ART, framework jars, JIT) is within ±1 MB.

Verdict:
- Within a few %: QML types (exact at login, splash-only at wallet), GPU, ART, fonts and all Qt/StatusQ libraries.
- Not within a few % (−4.5% native, −7.5% PSS at login): replay is the QML-side floor without Nim. The missing part is Nim's code pages in the APK (8–10 MB) and Nim's heap (4–9 MB, which grows after login). For QML-only memory work replay is a faithful stand-in; to reconstruct the real total, add those two Nim figures, which this calibration measured.

## Phase 4: clean Redmi calibration #1 (before the phases 3–4 review fixes)
Changes behind it:
- bd88bd3952 script-only mode: STATUS_RECORD_SCRIPT without STATUS_RECORD logs in with the recorder off. RECORDER=1 keeps record mode in release builds (-d:statusRecorder). The replay variant is profileable.
- 18efa7230b the replay package reads status-env.txt; android-record.sh works without run-as.
- 785aa5a8a9 the probe reads the app's own /proc/self/smaps, which gives per-mapping PSS in non-debuggable packages.

Setup:
- Redmi A5, UI process, TOTAL PSS+SwapPss from dumpsys meminfo.
- Packages:
  - Real: app.status.frecordr. BUILD_VARIANT=pr release with RECORDER=1, non-debuggable, profileable, R8. Nim built -d:release -d:production -d:statusRecorder.
  - Replay: app.status.freplayr. BUILD_VARIANT=replay release, non-debuggable, profileable.
  - Both are full-clean builds of 785aa5a8a9. A guard checks that the replay APK has no Nim or stub libs and the real APK has no replay main lib. A quick rebuild had contaminated both APKs; that run was discarded.
- Recording: made on the Redmi by a separate, unmeasured run of app.status.frecordr (test profile created by scripted onboarding, then login, then wallet). It is not the Lenovo recording.
- Real runs: script-only, with the same login.json as the recording run and the recorder off. libStatusReplay is loaded on both sides for the script and probe.
- Replay runs: realtime replay of that recording.
- Each package got one unmeasured warm-up run, then n=3 alternating (real, replay).
- Measured at +25 s (login screen; the script types the password at +30 s) and +160 s (wallet, about 120 s after the wallet click). Medians, with [min–max].

| MB | login real | login replay | Δ | wallet real | wallet replay | Δ |
|---|---|---|---|---|---|---|
| TOTAL PSS+SwapPss | 233.9 [232–236] | 225.6 [214–226] | −8.3 (−3.5%) | 301.8 [299–307] | 282.8 [274–286] | −19.0 (−6.3%) |
| Native Heap | 84.6 | 81.8 | −2.9 (−3.4%) | 138.2 [133–141] | 126.3 | −11.9 (−8.6%) |
| .apk mmap (all our + Qt libs) | 88.3 | 78.1 | −10.2 | 90.3 | 81.8 | −8.5 |
| Dalvik Heap | 5.4 | 2.7 | −2.6 | 7.2 | 6.7 | −0.4 |
| .so mmap (system) | 23.7 | 24.1 | +0.5 | 24.0 | 24.6 | +0.6 |
| .jar+.art | 8.2 | 9.9 | +1.5 | 11.1 | 13.3 | +2.2 |
| EGL mtrack | 16.1 | 19.2 | +3.1 (noise: 16–19 on both sides) | 12.3 | 12.3 | 0 |
| Unknown | 3.6 | 3.5 | 0 | 11.1 | 9.8 | −1.3 |

Per mapping (in-process smaps, medians):
- base.apk: −10.2 (login) / −8.5 (wallet). Embedded libs all map as base.apk. The replay APK lacks libnim_status_client (29.7 MB file) and libstatus_stub; everything else in the APK is identical.
- malloc: −3.1 / −5.9. This is Nim's heap (useMalloc).
- GLES_mali: 12.7 vs 12.7, equal.
- Fonts, hwui and stacks: equal.
- ART/framework: +1.6 / +2.2. The replay process maps more of the shared framework, because zygote-shared pages are divided among fewer sharers.

QML types (probe):
- Login: identical, 1239 loaded / 115 instantiated.
- Wallet: 1241 loaded on both sides; 332 instantiated real vs 327 replay. The 5 extra are the login splash: SplashScreen, DidYouKnowSplashScreen, DidYouKnowMessages, StatusProgressBar, Pane.
- Probe malloc: 91.2 vs 87.7 MB at login, 140.7 vs 134.1 MB at wallet.

Matches within a few %:
- QML types: exact at login; the splash is the only difference at wallet.
- GPU driver (GLES 12.7 both); EGL at wallet.
- System .so, fonts, hwui and Unknown at login.
- Native at login: −3.4%.

Does not match:
- APK pages: −8.5 to −10 MB, Nim's code (libnim + stub) absent.
- Nim heap: −3 MB at login, −6 to −12 MB at wallet. The real side varies 133–141 because Nim's heap keeps growing after login.
- Dalvik at login: −2.6 MB, no StatusGoStub/IPC Java side.
- Net: TOTAL −3.5% at login, −6.3% at wallet. The whole gap is Nim and the Java status-go stub.

So replay is the QML-side floor of the real app: equal for QML, Qt, GPU and fonts, minus Nim's code and heap. Add the measured Nim figures to reconstruct the real total.

### Follow-ups from the lead
- (a) **Recorder state on the real side:**
  - Both clean calibrations (#1 above and #2 below) ran the real side script-only: STATUS_RECORD unset, no hooks installed, nothing observed or written.
  - libStatusReplay is still loaded on both sides, for the script and the probe.
  - "Plus the recorder" in the first-pass smaps note refers only to the superseded debug pass.
- (b) **Debuggable builds:**
  - A debuggable Gradle type adds about 14–26 MB of ART on both sides.
  - For memory work, use `make mobile-record-run` / `mobile-replay-run` with `MOBILE_REPLAY_PROFILE=release`. That gives non-debuggable, profileable packages (pr-style record side), comparable to the floor work's 152.7 MB.
  - Documented in docs/replay-mode.md.

## Phases 3–4 review fixes (review-replay-p34)
- 8a3aada80a CRITICAL settings snapshot:
  - Fallbacks are off. Only unkeyed groups and groups keyed by the logged-in profile's pubKey/keyUid are kept.
  - The file is written without keyed groups at attach and rewritten at `loggedIn`.
  - Test seeds a foreign profile, an address, a community and an org-wide fallback key; all are absent from the snapshot.
  - New Redmi recording: 8 groups, test profile only. No desktop recording holds a settings.ini now (desktop-2's was deleted by the lead).
- 52f8be33a4 #2 and #11:
  - A RECORDER buildConfigField (false in normal builds) gates status-env.txt; the replay variant already read it (18efa7230b).
  - ANDROID_EXTRA_LIBS keeps the original order in normal builds.
- f2980c2a4c media:
  - #3 keys on the requested URL; redirect test.
  - #4 paletted PNG placeholders get a one-entry color table; Indexed8 and Indexed8+alpha test cases.
  - #5 header-only description, plus the GIF transparency flag.
  - #8 SHA-256 keys; URLs only in bytes mode.
  - #10 drained bodies are left out; test added.
  - #9 null-checked, ordered hook wrappers.
  - #14 QNetworkReply is forward-declared.
  - #6: new MediaHooksTest (StatusQ managers: off path, tap order, interceptor on/off) and a strict no-bodies test.
- 8fa82ca639:
  - #12 one queued ScriptRunner for replayed clicks; 5 s timeout in real time, 60 s while fast-forwarding.
  - #9 the media library lives for the whole process.
  - #6 the speed test is relative to a 1x run.
  - MOBILE_REPLAY_PROFILE=release.
  - A variant change clears staged Android libs. That bug had contaminated one APK pair during calibration #1; the run was discarded.
- #7: local nimqml-seaqt branches `replay-hooks-v0` (6c78457) and `replay-hooks-v1` (f2caa4f) keep the old pins reachable.
- #13: the doc defines the probe's "loaded" and "instantiated".
- Tests: host 9/9 under ASan; `make run-replay-tests` (with StatusQ) 11/11.

## Phase 4: clean Redmi calibration #2 (final, after the review fixes)
Same protocol as #1, with full-clean release builds of 8fa82ca639:
- A new recording on the Redmi, made by an unmeasured run of frecordr (filtered settings, placeholder media).
- One warm-up per package, then n=3 alternating.
- Real side script-only (recorder off).
- Measured at +25 s and +160 s.

| MB, median [min–max] | login real | login replay | Δ | wallet real | wallet replay | Δ |
|---|---|---|---|---|---|---|
| TOTAL PSS+SwapPss | 233.7 [227–243] | 219.7 [220–234] | −14.0 (−6.0%) | 295.6 [286–344] | 282.6 [277–302] | −13.0 (−4.4%) |
| Native Heap | 85.0 | 81.2 | −3.8 | 132.8 [133–141] | 130.8 [126–134] | −1.9 |
| .apk mmap | 84.0 | 80.6 | −3.4 | 85.8 | 84.9 | −0.9 |
| Dalvik Heap | 5.4 | 2.7 | −2.7 | 7.2 | 6.7 | −0.5 |
| EGL | 19.2 | 18.4 | −0.8 | 12.3 | 12.3 | 0 |

Probe:
- Types identical at login (1239/115). Wallet: 1241 loaded on both sides; 332 vs 327 instantiated (the login splash).
- Probe malloc, stable across runs: login 91.3 real vs 87.3 replay (−4.0); wallet 139.3 vs 133.9 (−5.4).
- In-process smaps: malloc −3.7 / −5.4, base.apk −3.3 / −0.9, GLES/fonts/hwui equal.

Pooled #1+#2 (n=6 per side; both had the recorder off on the real side):
- Login: TOTAL 231.7 real vs 228.4 replay (−1.4%); native 85.0 vs 81.3 (−3.7, sd 0.2–0.4).
- Wallet: TOTAL 293.5 vs 287.6 (−2.0%); native 132.9 vs 126.6 (−6.3, sd 3).
- Run-to-run sd of TOTAL is 10–24 MB and of .apk mmap 5–7 MB on both sides. That is file-page residency on a 4 GB device, so APK page differences, including Nim's code pages, can't be resolved from TOTAL at n=3–6.

**"Replay + Nim = real" constant:**
- Stable part: native/malloc. At login +3.7 MB (Nim heap before login); at wallet +5.4 MB (probe malloc) to +6.3 MB (meminfo native), Nim heap growing.
- Plus Dalvik +2.7 MB at login (the Java status-go stub).
- Nim code pages: +8 to +10 MB when measured with stable smaps (debug pass), but lost in residency noise in release TOTAL.
- So within noise, release TOTAL real ≈ replay + 4–14 MB.
- What matches within a few %: QML types, GPU (EGL, GLES), fonts, system libs, ART.
- For memory work on the QML side, compare replay against replay. Only for absolute numbers add the native constant, using n≥5 because of the TOTAL noise.

## Final review fixes (final-review-replay)
- 3637f77a03 ScriptRunner:
  - A queued optional click gives way only when the next step's item shows up while it waits, so a click on a timer-opened popup is no longer dropped.
  - Each queued step's timeout counts from when it becomes current.
  - Tests cover both, plus a step queued behind a failing one that still waits its full timeout.
  - has_profile checks an explicit `profileFound` moment.
- 4ac675dcf8 build:
  - The ship guard checks only `buildConfig/${BUILD_TYPE}` and the APK/AAB this gradle run produced, and requires unzip.
  - The Nim library and the APK each have their own settings stamp.
  - No Record APK name on iOS.
  - MOBILE_REPLAY_PROFILE=release refuses STATUS_APP_KEYSTORE_* on the make command line and stops early, with the keytool command, when the debug keystore is missing.
- Verified on the buildbox, sequence debug record → normal release → replay → normal release:
  - No false guard failure.
  - Nim rebuilt only on the settings change, not for replay or the repeat.
  - StatusPR.apk has no libStatusReplay; StatusReplay.apk has no libnim.
- Tests: host 9/9 under ASan; StatusQ-linked 11/11; Nim recorder test 3/3.

## Tidy-up after approval (83b633eccc)
- buildApp removes earlier gradle outputs before building, so no stale recorder APK/AAB can be checked or copied.
- The release signing checks stop at parse time.
- A missing unzip is only a warning for normal builds.
- RECORDER=1 is refused on iOS.
- The doc states the N×60 s fast-forward stall bound.
- aStepQueuedBehindAFailingOneRunsAfterIt now fails on the old queued-time rule; I checked it against 999dd8541f's runner.
- Tests: host 9/9 under ASan; StatusQ 11/11.

## Final summary
**What exists:**
- `make record-run` / `make replay-run` (desktop).
- `make mobile-record-run` / `make mobile-replay-run` (Android), with `MOBILE_REPLAY_PROFILE=release` for non-debuggable, debug-keystore-signed packages.
- Recording is unattended: scripted onboarding/login with the test password; QML-read-driven state; settings snapshot of the recorded profile only; media as same-size placeholders (bytes opt-in).
- Replay runs without Nim, status-go or the service; on desktop and Android (Lenovo, Redmi).
- Design: docs/replay-mode.md and docs/adr/0011.

**Calibration (Redmi, release, real side with the recorder off):**
- QML types identical at the login screen. At wallet only the 5 login-splash types differ.
- GPU, fonts, system libs and ART match.
- real ≈ replay + Nim heap (+3.7 MB login, +5–6 MB wallet) + Java stub (+2.7 MB login) + Nim code pages (lost in release TOTAL noise, sd 10–24 MB).

**Blocker:** the nimqml-seaqt hooks exist only as a local commit. A seaqt PR and pin bump need the owner's go before this can go upstream.

**Known limits:**
- Hand-driven input isn't replayed.
- Slot calls don't change replayed state.
- OS permission state is scripted, not recorded.
- Drained XHR bodies aren't recorded.
- Recordings from real accounts must stay local.
