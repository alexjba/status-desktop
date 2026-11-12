# mem-r1: first memory round on replay builds (#21528 / #22657)

Status: FINAL (2026-11-09).

- Branch `spike/floor-replay` (edit worktree `~/Repos/status-desktop.worktrees/spike-floor-replay-edit`; builds run in `~/Repos/status-desktop.worktrees/spike-floor-replay`, detached). Signed, not pushed. Tip 011dcb74a2.
- Measurement-only branches: `spike/floor-replay-mark` (0c6c4d7297 = stack with the first L3 variant + w-final's marker commit), `spike/floor-replay-diag` (probe: glyph scan, type-load timer, popup-open timer), `spike/floor-replay-diag-nol3`.
- Work dir `spikes-floor/mem1/` (scripts, logs, APKs, device data under `dev/` and `real/`).

## TL;DR
- Four "load on demand" levers on top of floor-final + the replay tooling. No behaviour change apart from where the type is loaded.
- **Real app, Redmi A5, release (pr), n=5 alternating vs floor-final (same package/harness as w-final): login TOTAL 147.2 → 144.9 (−2.3), post (+120 s, wallet) 248.9 → 242.0 (−6.9).** Stable components at login: native −2.4, .apk −0.9, CJK font −0.6 (= −3.9); post: native −3.2, malloc −3.3, .apk −0.6, CJK −0.6.
- **≤150 at login:** both arms are under 150 in this session (base 147.2 vs w-final's 152.7; TOTAL sd 3–5 MB at n=5). Applying the stable −3.9 to w-final's 152.7 gives ≈ 148.8; applying the measured TOTAL −2.3 gives ≈ 150.4. So: reached on the stable components, borderline on raw TOTAL.
- Speed: am start −39 ms, ENTER→wallet −132 ms, idle CPU same. First load of every by-URL type ≤ 28 ms; first language-dropdown open +5..10 ms (L3).
- Two new finds: the "CJK probe" (hidden language list shaping 한국어) and `import QtMultimedia` in StatusSoundEffect (QtMultimedia QML plugin + Qt6MultimediaQuick at login).
- Bigger remaining eager libs at login (not done, need C++/Nim changes): Qt6Multimedia 0.8 MB, libMobileWebView 0.7 MB (+WebChannel 0.2), Qt6Nfc 0.2 MB, image-format plugins 0.3 MB.

## Gate 1: port + baseline
- Port: spike/floor-final 27d06db05e + the 32 replay commits of feat/replay-mode (0b5031d868..83b633eccc), cherry-picked, signed.
  - nimqml-seaqt: floor-final pins e75d08c (nimqmlNoWidgets), replay pins 1447e1e (hooks on fa084a8). New local commit 6b44f89 = e75d08c + the hooks commit, branch `replay-hooks-floor` in the worktree's submodule; the three pin commits resolve to it. Not reachable from other clones (same blocker as feat/replay-mode).
  - `ui/StatusQ/src/networkaccessfactory.cpp`: replay's interceptor/tap merged around floor-final's PlatformHttps branch (interceptor first, tap on whichever reply is returned).
  - Fresh-worktree gotchas hit: submodules with staged deletes (prl-to-pc, semver.nim, status-keycard-qt, uuids → `reset --hard`), seaqt full-SHA fetch, status-go `nimble setup`.
- `make run-replay-tests`: 11/11 (desktop, Qt 6.11.1).
- Recordings (local, gitignored): `Status/recordings/desktop-1` (`make record-run`), `lenovo-1`, `redmi-1` (release record package app.status.fmemr = pr + RECORDER=1; e2 kit + own-lib order files as w-final). Lenovo replay smoke OK (login, wallet).
- Packages: app.status.fmemr (record/real, pr release), app.status.fmemrp (replay, release, profileable). Build script `mem1/abuild.sh <record|replay|real> <tag> [ref]`.

Mac replay probe (desktop-1, frozen moment, 400x860, qmlmeta probe): login 697 types / 109 inst / load cost 18.33 MB; wallet 762 / 315 / 20.45 MB.

Redmi replay baseline (rp0, realtime replay of redmi-1, n=3, no reinstall): login TOTAL 161.8 / 151.0 / 148.3, native 55.6, probe malloc 61.2, 672 types / 112 inst; wallet TOTAL med 240.6, native 122.8, malloc 130.6, 738 / 327.

## Levers (commits on spike/floor-replay)
| # | commit | what | Mac (login load cost) | Redmi replay | first open (Redmi) |
|---|---|---|---|---|---|
| L1 | 4d2668365f | 13 rare popups/views by URL: CommunityFetchPopup, Receive, remove-account confirmation, enable push / biometrics, third-party services (handler + onboarding), user status menu, WelcomePage (first run), banned / control-node-offline / join-community views, LogosNetworkView, SavedAddressesView | 697 → 675 types, −0.66 MB (wallet −0.73) | alone, n=3: native −0.7 login, malloc −2.1 wallet; types 672 → 650 / 738 → 718 | 6–28 ms (SavedAddresses 28, WelcomePage 17, Receive 17, rest ≤ 14) |
| L2 | 4ab717be3f | privacy walls not precompiled with third-party services on (their loaders precompile them otherwise); mobile browser settings menu by URL | 675 → 664, −0.30 MB | in stack | menu 6 ms, wall 16 ms (privacy mode only) |
| L3 | d7b7a61f93 | language selector sets the native names (한국어…) on its first open; delegates built as before | (CJK not visible on Mac) | NotoSansCJK .ttc 0.6 → 0 at login and wallet (n=3 first variant, n=4 final variant) | open() +5..10 ms |
| L4 | 011dcb74a2 | StatusSoundEffect without `import QtMultimedia` (SoundEffect wrapper in `private/StatusQtSoundEffect.qml`, created on the first non-native play) | — | Qt6MultimediaQuick 0.3 MB + QtMultimedia QML plugin gone at login (raw smaps) | never loaded on Android |
| — | 575d9f96ab | tooling: probe keeps raw smaps (`STATUS_REPLAY_PROBE_RAW_SMAPS=1`) | | | |
| — | 504bc6178b | tst_PopupsEmojiPicker: no community-fetch popup warnings to expect | | | |

### Gate 3: Redmi replay A/B of the stack (rp0 vs L1–L4, n=3 alternating, reinstall before each run, medians, MB)
| stage | arm | TOTAL+swap | native | .apk | .so | Other mmap (CJK) | probe malloc | types / inst |
|---|---|---:|---:|---:|---:|---:|---:|---|
| login | rp0 | 153.6 | 55.8 | 51.5 | 18.1 | 0.6 | 61.9 | 672 / 112 |
| | stack | 147.5 | 53.7 | 50.6 | 17.6 | 0.0 | 61.4 | 641 / 108 |
| | Δ | −6.1 (noise) | **−2.1** | **−0.9** | −0.5 | **−0.6** | −0.5 | −31 |
| wallet | rp0 | 249.8 | 126.2 | 56.8 | 18.9 | 0.6 | 130.5 | 738 / 327 |
| | stack | 242.0 | 121.7 | 56.0 | 17.9 | 0.0 | 127.6 | 711 / 323 |
| | Δ | −7.8 (noise) | **−4.5** | **−0.8** | −1.0 | **−0.6** | **−2.9** | −27 |
- TOTAL is not usable at n=3: `.so mmap` (system libs) swings 13–22 MB between runs of the same APK. Ranked on native, .apk, Other mmap, malloc, types.
- Intermediate A/Bs: L1 alone (above); L1–L3: malloc −1.5 login / −2.8 wallet, CJK gone.

### Gate 4: real app, Redmi A5 (pr release, pkg app.status.fmemr, w-final's m.sh/job.sh, VISITS=0, n=5 alternating, medians, MB)
BASE = spike/floor-final-mark d252f68590. MEM = spike/floor-replay-mark 0c6c4d7297 (stack + markers). Same kit (e2) and order files, same build script.

| stage | arm | TOTAL | native+swap | malloc | .apk | .so | EGL | swap | majflt |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| login (37 s) | base | 147.2 | 54.7 | 59.6 | 54.4 | 13.8 | 7.6 | 1.1 | 69 |
| | mem | **144.9** | 52.3 | 57.5 | 53.5 | 14.9 | 7.6 | 1.1 | 60 |
| | Δ | **−2.3** | **−2.4** | **−2.1** | **−0.9** | +1.1 (noise) | 0 | 0 | |
| post (+120 s) | base | 248.9 | 122.6 | 127.0 | 61.6 | 15.4 | 8.2 | 0.7 | 241 |
| | mem | **242.0** | 119.4 | 123.7 | 61.0 | 16.0 | 8.2 | 0.7 | 84 |
| | Δ | **−6.9** | **−3.2** | **−3.3** | −0.6 | +0.6 | 0 | 0 | |
- Per run TOTAL login: base 144.2 / 145.3 / 147.2 / 152.3 / 157.0 (sd 5.4), mem 142.8 / 143.2 / 144.9 / 147.3 / 150.0 (sd 3.0). Post: base 244.3–255.6 (sd 4.8), mem 239.3–247.1 (sd 3.5).
- Other mmap (CJK .ttc): base 0.56–1.04 MB → mem 0.03 MB at login and post.
- Speed (medians n=5): am start 1827 → 1788 ms, ENTER→wallet 4474 → 4342 ms, idle CPU login 25.15 → 24.95 %, post 1.20 → 1.23 %.
- Every run passed the login-screen and wallet checks (`real/rj{1,2,3}.log`, screenshots under `real/dev/redmi/runs/`). Both arms show the "To open app menu" hint over the wallet at post.
- Jobs rj1/rj2 took 27 min each (4 runs), 2 min over the 25-min cap.
- Raw data: `real/dev/redmi/{rows,time}.csv`; medians `python3 real/summ.py real/dev/redmi base mem`.

## CPU / first open
- By-URL types: Redmi replay at wallet +60 s, 6 launches alternating forward/reverse order (`STATUS_REPLAY_PROBE_TYPELOAD`), max ms: SavedAddressesView 28.2, WelcomePage 17.4, ReceiveModalInstance 16.6, BrowserPrivacyWall 15.9, RemoveAccountConfirmationPopup 13.9, UserStatusContextMenu 13.6, Banned/ControlNodeOffline views 10.9, CommunityFetchPopup 10.7, LogosNetworkView 10.1, EnableBiometrics/ThirdpartyServices/JoinCommunityView ≤ 7.9, MobileSettingsMenu 5.8. All under 30 ms.
- **L3 language dropdown first open** (Redmi replay, login screen, 4 launches per arm alternating, `STATUS_REPLAY_PROBE_OPEN`):
  - first variant (list model only once opened): open() 34–43 ms / first frame 35–89 ms vs 4–12 / 26–43 ms without L3 → +25..45 ms, over budget; replaced.
  - final variant (delegates as before, native names set on first open): open() 8–16 ms / first frame 13–50 ms vs 4–7 / 14–46 ms → +5..10 ms. NotoSansCJK not mapped at the login screen in any of the 4 runs.

## Functional / tests
- Host (Qt 6.11.1 offscreen, Basic): tst_OnboardingLayout 55/55 (1 skip), tst_StartupOnboardingWrapper 8/8, tst_StatusLanguageSelector 9/9, tst_LanguageView 7/7, tst_StatusSoundEffect 6/6 (plays through the new wrapper on macOS), tst_PopupsEmojiPicker 9/9, tst_ProfileButton 6/6, tst_LeftTabView 5/5, tst_AccountOrderSync 3/3, tst_WalletLoaderDeepLink 3/3, tst_ChatLoaderSection 7/7, tst_HiddenCommunitySection 5/5, tst_WalletAccountDelegateShell 4/4; PagesValidator OK.
  - The first L3 version (model only while the dropdown is visible) destroyed the delegate inside its own click handler when the dropdown closed without a transition (offscreen tests) → `languageSelected` never emitted. The final variant keeps the delegates and passes all three suites.
- Device: login and wallet render in every replay and real run; every by-URL type loaded without errors on the Redmi (type-load probe).
- L3 on the Redmi: the language dropdown opened at the login screen shows every name incl. 한국어 (`dev/redmi/opentime2/rp-d3b-*.png`).
- L4 on the Redmi (real app, mem APK): Settings → Notifications, drag the volume slider → logcat `SoundPlayer: playing sound 1` 1.7 s after the swipe (release), no Sound error / FATAL (`real/dev/redmi/sound/`, script `real/sound4.sh`).
- Not done: opening each converted popup/view through the UI on a device (only loaded by the probe); desktop app run of L4's play path (covered by tst_StatusSoundEffect on macOS); Banned/ControlNodeOffline/Join community views need such a community.

## Risks
- L1/L2: a wrong URL fails only at first open (all loaded by the probe on the device). DeferredComponent bindings are Qt.binding; signal wiring moved to onCreated/onLoaded.
- L1 WelcomePage: OnboardingFlow now starts with no initial item and replaces in Component.onCompleted when there is no profile (tests pass).
- L3: +5..10 ms on the first language dropdown open; the CJK font then loads (as it did at startup before).
- L4: any new `import QtMultimedia` in the login closure brings the 0.3 MB back (same rule as the media lever).
- The real-app A/B (mark branch) and the gate-3 replay stack ran the first L3 variant. Memory at login/post is the same for both variants (dropdown never opened; CJK absent in both, checked on the device); the final variant changes only the first-open path.

## Fixed-floor items at the login screen ≥ 0.3 MB (replay smaps/probe, Redmi), ranked
| # | item | MB at login | persists after login | status / proposed fix |
|---|---|---:|---|---|
| 1 | QML type data of the precompiled closure (641 types after L1–L4) | ≈ 21 (17.4 Mac × 1.26) | yes | remaining never-instantiated ≈ 4 MB Mac, but hot chat/community/home (>30 ms first open). Next lever is Qt-side: property-cache sharing #22676 (≈ −3.3..3.7) |
| 2 | Qt6Multimedia core, eager | 0.8 | yes | not built. Drop `QT += multimedia` link from the wrapper main lib (no symbol used), replace StatusQ's one `QtAudio::convertVolume` call, compile nim-qtcamera's Qt camera path out on Android (CameraX is used), System.loadLibrary + JNI_OnLoad in MediaBackend.ensureLoaded(). Nim + C++; replay can only see the StatusQ/main-lib part |
| 3 | libMobileWebView (+ Qt6WebChannel 0.2, WebChannelQuick ≈ 0.05) | 0.7 (+0.25) | yes until the Browser is opened | not built (real A/B ended ≤ 150). Sketch below |
| 4 | NotoSansCJK fallback font | 0.6 + FT data | yes | **L3, done** |
| 5 | Qt6MultimediaQuick + QtMultimedia QML plugin | 0.3 + type registrations | yes | **L4, done** |
| 6 | image-format plugins (qwebp 0.2, qjpeg 0.1, qgif/qsvg) | 0.3 | qwebp loads at the wallet anyway (webp token logo) → ≈ 0.1 persistent | not built: UrlUtils ctor calls `QImageReader::supportedMimeTypes()`, which instantiates every plugin; make the image lists lazy |
| 7 | Qt6Nfc, eager | 0.2 | yes | below threshold; `QT += nfc` in the wrapper, no symbol used; needs JNI_OnLoad when keycard loads + a real NFC test |

Malloc owners by subsystem (not re-measured here; heapprofd of the login screen in login-report, Lenovo, before floor-final): QML type data 26.1 MB (property caches 7.4, tryDone 3.6, unit generation 2.1, imports 1.6, registerType 1.3, setCachedUnit 1.2), ART/JNI 5.3, GPU driver 4.3, login-UI instances 2.7 (login-only), StatusQ C++ 2.3 (QMimeDatabase 1.85, since fixed by w-cheap), network/TLS 2.1 (since reduced by w-ssl), fonts 2.1 (CJK part fixed by L3), HWUI 1.7. Replay probe malloc at login now 61.4 MB (vs real 57.5 MB alloc in meminfo).

### libMobileWebView: dlopen on first Browser use (written up, not built)
- Estimate: −0.7 MB (.apk pages: 280 KB of the first r--p segment = dynsym/relocations, 336 KB of .text, 52 KB relro/data) − 0.2 Qt6WebChannel − ≈ 0.05 WebChannelQuick + plugin, ≈ −0.9..1.0 MB at login and after login until the Browser is opened; nothing after a Browser visit.
- Why it is loaded: StatusQ links it (`target_link_libraries(StatusQ PRIVATE MobileWebView)`) to register `MobileWebViewBackend` in `StatusQ.CustomWebView` (typesregistration.cpp) and to ask `MobileWebViewCapabilities` (browserbackendcapabilities.cpp). And BrowserLayout (precompiled at login) reaches `MobileWebViewAdapter.qml` (`import StatusQ.CustomWebView`) and `ConnectorBridge/Eip1193ProviderAdapter.qml` (`import QtWebChannel`).
- Change:
  1. Move the registration into a small QML extension plugin for `StatusQ.CustomWebView` (its own lib, linking MobileWebView), so the engine loads it on the first import; StatusQ drops the link. `isInPageMediaPlaybackSupported` becomes a lazy dlsym or a static fact per platform.
  2. Load the webview adapter and the WebChannel bridge by URL when the first tab creates a webview, so precompiling BrowserLayout no longer imports either module.
  3. Android packaging: the plugin in the APK's qml plugin list (androiddeployqt), MobileWebView + WebChannel out of the eager list (trim_qt_deploy follows DT_NEEDED); MobileWebView's Java side and any JNI_OnLoad must still be registered (plugin load via QLibrary does not run JNI_OnLoad → call it as MediaBackend does).
- Risks: first Browser open +lib load (est. 20–40 ms, measure); iOS/desktop keep linking statically/differently; JNI registration order; the browser's precompile benefit shrinks for the adapter part.

## Notes
- Probe "instantiated" = an executable unit exists; QmlCompiler's precompile creates them too, so a type can show as instantiated without any object, and objects inside a never-created parent (Popups.qml before the first popup) show as not instantiated.
- Mac `QT_QUICK_CONTROLS_STYLE=Basic` replay gives a closure closer to Android (639 types at login).
- Mac replay cannot see StatusQ QML changes without rebuilding the desktop libStatusQ (StatusQ QML is in its qrc).
