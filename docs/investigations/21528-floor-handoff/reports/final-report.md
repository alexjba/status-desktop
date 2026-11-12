# w-final: all fixed-floor levers stacked (#21528 / #22657)

Status: FINAL (2026-11-08). Branch `spike/floor-final` (worktree ~/Repos/status-desktop.worktrees/spike-floor-final), 21 signed commits on spike/floor-r3-base e1f906462e, tip 27d06db05e. Not pushed. Package app.status.ffinalr (release-like pr variant, R8, non-debuggable). Work dir spikes-floor/final/.

## TL;DR
- **Login screen 189.6 → 152.7 MB (−36.9). Target 150: 2.7 MB short.**
- **Post-login (+120 s, wallet) 286.9 → 244.9 (−42.0).** After visiting chat, communities and settings, back on the wallet: 331.1 → 282.3 (−48.8). The saving holds after login and grows after visits.
- After a Browser visit 448.6 → 415.9 (−32.7). The window-surface lever flips back there: EGL and GPU return to base.
- Faster everywhere: am start −456 ms, ENTER→wallet −198 ms, chat first open −100 ms. Idle CPU is the same.
- Functional smoke on the Lenovo passes: onboarding, login, wallet with token logos, chat, communities, settings, Browser open/close, QR scanner (live frame with torch), UI sound (SoundPool). No FATAL or UnsatisfiedLink.

## Redmi A5 A/B (release, n=5 per arm, alternating base/final, medians)
BASE = spike/floor-r3-base with the prebuilt Qt 6.11.0 kit. FINAL = spike/floor-final with the e2 kit and the own-lib order files (`~/Qt-custom/order-files/{libStatus,libnim,libStatusQ}.order`, `STATUS_LIB_SECTIONS=1`, no Qt order). Both are built with the same final/rbuild.sh into the same package. Both carry the measurement-only marker commit 0468f51f17 (branches `spike/floor-*-mark`).

MB; TOTAL = PSS + SwapPss; GPU = `dumpsys gpu` Proc total.

| stage | arm | TOTAL | PSS | native+swap | .apk | .so | EGL | GPU | swap | majflt |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| login (37 s) | base | 189.6 | 188.5 | 63.2 | 77.0 | 18.7 | 12.3 | 41.2 | 1.1 | 609 |
| | final | **152.7** | 151.6 | 54.8 | 54.4 | 17.4 | 7.6 | 21.9 | 1.1 | 353 |
| | Δ | **−36.9** | −36.9 | −8.4 | −22.6 | −1.3 | −4.7 | −19.3 | 0 | |
| post (+120 s wallet) | base | 286.9 | 282.1 | 128.3 | 83.6 | 19.7 | 12.3 | 48.5 | 2.2 | 1020 |
| | final | **244.9** | 240.3 | 116.4 | 61.2 | 17.7 | 8.4 | 29.3 | 1.4 | 503 |
| | Δ | **−42.0** | −41.8 | −11.9 | −22.4 | −2.0 | −3.9 | −19.2 | | |
| after chat, communities, settings → wallet | base | 331.1 | 315.7 | 154.9 | 84.6 | 19.7 | 12.3 | 50.6 | 14.3 | 1100 |
| | final | **282.3** | 277.9 | 142.3 | 64.6 | 20.4 | 8.5 | 30.6 | 3.0 | 1135 |
| | Δ | **−48.8** | −37.8 | −12.6 | −20.0 | +0.7 | −3.8 | −20.0 | | |
| + Browser → wallet | base | 448.6 | 401.4 | 183.7 | 111.4 | 24.0 | 20.1 | 77.4 | 51.7 | 6889 |
| | final | **415.9** | 370.6 | 174.6 | 91.0 | 23.0 | 19.8 | 76.6 | 42.4 | 5672 |
| | Δ | **−32.7** | −30.8 | −9.1 | −20.4 | −1.0 | −0.3 | −0.8 | | |

- Every run is valid. Each one passed the login-screen check and the wallet-visible check before and after visits and after the Browser. EGL was non-zero in every snap. No aborts and no reruns.
- Swap is noisy at the visit and browser stages:
  - visit SwapPss: base 1.9–23.6, final 0.7–34.6;
  - browser SwapPss: 28–76.
  - For those two stages, PSS and .apk are the cleaner signal.
- **Window-surface lever after the Browser.** Final EGL goes from 8.5 to 19.8 and GPU from 30.6 to 76.6, the same as base (20.1 / 77.4). The −4 MB EGL and −19..20 MB GPU saving of 0dcf254d9a is fully lost after the first Browser visit, as designed (one-way flip).
  - About −11 MB of the browser-stage TOTAL gap vs the visit stage comes from this.
  - The rest of the saving (.apk −20, native −9) holds.
- Raw data:
  - final/dev/redmi/{rows,time,gpu}.csv
  - final/dev/redmi/runs/{base,final}/ (meminfo -a, gpu, screenshots per stage)
  - medians: `python3 final/summ.py final/dev/redmi base final`

## Speed (same runs, medians n=5; ranges in brackets)
| | base | final | Δ |
|---|---:|---:|---:|
| am start TotalTime | 2292 (2073–2404) | **1836** (1737–1896) | **−456 ms** |
| ENTER→wallet-visible | 4193 (3906–4264) | 3995 (3685–4242) | −198 ms |
| idle CPU login screen, 20 s | 26.15 % | 25.45 % | −0.7 |
| idle CPU post, 60 s | 1.27 % | 1.25 % | same |
| chat first open (tap → first frame) | 1308 | 1208 | −100 ms |
| Browser first open | 961 | 933 | −28 ms |
| Communities portal first open | 397 | 393 | same |
| Settings/Profile first open | 1840 | 1874 | +34 ms (noise: ranges 1812–2065 vs 1793–2495, one 2495 outlier) |
- Login-screen major faults drop from 609 to 353 and post-login from 1020 to 503 (e2 + order files).

## Per-lever: expected (source reports, Redmi release) vs measured as a whole
| lever | commits | expected login / post | main component |
|---|---|---:|---|
| type closure by URL (w-login) | 929459d569 a3123f2476 1a880d974d 0c7e14d10c | native −2.2 / −2.6 | native |
| cheap: MIME, runtime compile, symbols font, file dialogs (w-cheap) | 2955455bde 4c92813a83 ab624e0b35 5a8a32da5c | native −2.9 / −4.7; .apk −1.3 / −1.2 | native, .apk |
| lazy media + SoundPool (media) | 68a7a71c49 2ae7860d27 92b73814c4 | .apk −3.6 / −3.3 (TOTAL −9.2 login was partly .so noise) | .apk |
| native QR (qr) | ba0eae49ae a50b5677d7 | 0 / 0 without a scan (−6.5 native, −6.6 .apk after a scan, n=1) | — |
| taken window surface (win) | 0dcf254d9a | −8.0 / −14.0 (EGL −4.7 / −3.9, GPU −19.6 / −17.8, native −0.9 / −2.3) | EGL, GPU |
| no OpenSSL in the UI process (ssl) | e4461b2842 b687bbeece 4315e886a7 ce0d934c96 84779865b7 | −10.8 / −8.6 (native −2.7 / −1.6, .apk −6.8 / −6.3) | .apk, native |
| e2 kit + own-lib order files (order) | a6d3b892a1 + kit/env | −12.8 / −12.6 (.apk −12.9 / −12.6) | .apk |
| **sum of expected** | | **≈ −41 / −47** | |
| **measured stack** | | **−36.9 / −42.0** | |

By component:

| component | expected sum login / post | measured login / post |
|---|---:|---:|
| native | −8.7 / −11.2 | −8.4 / −11.9 |
| .apk | −24.6 / −23.4 | −22.6 / −22.4 |
| EGL | −4.7 / −3.9 | −4.7 / −3.9 |
| GPU | −19.6 / −17.8 | −19.3 / −19.2 |

- The levers add up almost linearly.
- The ≈ −2 MB .apk shortfall is the expected overlap between media and ssl. Both remove Qt's FFmpegStub path, which loaded libcrypto at startup; once FFmpeg is lazy, ssl's `QT_INSTANT_LOAD_FFMPEG_STUBS` has nothing left to save.
- The −4..5 MB TOTAL shortfall vs the expected sum is that overlap plus the source reports' TOTAL noise (several of them quoted TOTAL medians inflated by .so residency).

## Functional (Lenovo TB520FU, FINAL APK, release)
- **Profile setup** (job.sh, Lenovo branch): onboarding → Create profile → password. "Save password" was OFF; screenshot dev/lenovo/setup/s4.png shows both toggles off. Confirm → wallet.
- **smoke** (final/smoke.sh, dev/lenovo/smoke-final-2329/), screenshots 01–11:
  - login → wallet;
  - token logos render (USDS, USDC EVM/BSC, SNT, ETH, DAI over PlatformHttps);
  - chat (empty state);
  - communities portal;
  - settings;
  - Browser renders status.app, then back to the wallet ("close");
  - QR scanner dialog opens: CameraX use case ACTIVE, back-camera combo shown;
  - logcat: 0 FATAL / UnsatisfiedLink / JNI ERROR / `is not a type`.
- **smoke2** (dev/lenovo/smoke2-2338/):
  - QR scanner with the torch on shows a **live camera frame** in the preview (qr-torch.png). The first smoke's frame was black only because the room was dark.
  - Settings → Notifications page reached.
- **UI sound (smoke4, dev/lenovo/smoke4-0149/).** Settings → Notifications, drag the volume slider (release calls `Global.playNotificationSound()`).
  - logcat shows `SoundPlayer: playing sound 1` 0.65 s after the drag.
  - AudioTrack is created for pid 15646.
  - `dumpsys audio` lists a SoundPool player for our uid/pid (USAGE_MEDIA, CONTENT_TYPE_SONIFICATION).
  - No Sound error. The smoke3 attempt didn't scroll to the slider, so it didn't count.
- **Mac host:**
  - `make run-statusq-tests`: 32/41 at first. The 9th failure was TestFonts (`registeredFonts` expected 5 font files; the bundled StatusSymbols.ttf makes 6). That is a test expectation, fixed in **27d06db05e** (signed); TestFonts now passes.
  - The 8 remaining failures are the known environment set, identical to w-cheap's run:
    - tst_userscripts_manager;
    - 6 WebView/WebEngine offscreen segfaults;
    - StatusQQmlTests 232/234 (StatusImageCropPanel native-style warnings).
- **Storybook QmlTests** (Qt 6.11.1 offscreen, Basic style). All pass:

| test | result |
|---|---|
| tst_OnboardingLayout | 55/55, 1 skip |
| tst_StartupOnboardingWrapper | 8/8 |
| tst_DAppsWorkflow | 37/37, 1 skip |
| tst_ConnectDAppModal | 8/8 |
| tst_PopupsEmojiPicker | 9/9 |
| tst_ShareFlowLoader | 15/15 |
| tst_ChatHeaderMenu | 6/6 |
| tst_ChatViewThreadPanel | 12/12 |
| tst_StatusStickersPopup | 6/6 |
| tst_ProfileButton | 6/6 |
| tst_DownloadsListView | 9/9 |
| tst_BrowserFormatSupportContext | 9/9 |
| tst_StatusFileDialogs | 5/5 |
| tst_StatusChatInput | 56/56 |
| tst_AppSearch | 23/23 |
| tst_BuyCryptoModal | 7/7 |
| tst_WalletLoaderDeepLink | 3/3 |
| tst_StatusSectionLayout | 7/7 |

  - PagesValidator: all pages OK.
  - Logs: final/host.log, host-statusq.log, host-sb.log.

## Conflicts resolved
- **`mobile/scripts/android/trim_qt_deploy.py`** (media 2ae7860d27 vs ssl 4315e886a7), in 9da148e9cc:
  - Kept ssl's `closure()` and its trimming of lazy `bundled_libs`, with media's `is_lazy()` skip inside `closure()` and media's "eager libs need lazy libs" guard after it.
  - ssl's list is renamed `lazy_bundled` so it no longer shadows media's compiled-regex `lazy`. The first FINAL build failed on this (`'str' object has no attribute 'search'`); the fix is squashed into the commit.
  - Output on FINAL: `lazy bundled_libs = ssl_3 crypto_3 status-keycard-qt`; `lazy (packaged, loaded on first use) = FFmpegStub-crypto/ssl, avcodec, avformat, avutil, ffmpeg plugin, swresample, swscale`.
- **`mobile/android/qt6/proguard-rules.pro`** (SoundPlayer/QrCamera vs PlatformHttps): union of the three keep rules.
- **ce0d934c96 was parented on the dropped diagnostic 1399dd7597** (wrapperApp smaps dump). There is no dependency; it applied without it.
- **Combinations checked by reading the code, with no textual conflict:**
  - ssl's `QT_INSTANT_LOAD_FFMPEG_STUBS=1` together with media's lazy FFmpegStub-*: both are harmless together.
  - win's `application hardwareAccelerated=false` with CameraX: ImageAnalysis only, no Android View, so the QR camera works; verified on the Lenovo.
  - win's flag with SoundPool: SoundPool uses no view.

## Fresh-worktree notes (for the next integrator)
- The nimqml-seaqt submodule fetch must use the full SHA (`e75d08cb66e37d9bbeb835042141ef4720a43ce5`); the abbreviated one fails.
- 4 submodules came out of the first recursive init with an empty working tree and staged deletes: prl-to-pc, semver.nim, status-keycard-qt, uuids. Fix: `git -C vendor/<s> reset --hard HEAD`.
- status-go needs a one-time `nimble setup --localdeps` (memory gotcha); otherwise make tries to rebuild libstatus.so and fails on `string.h`.

## Follow-ups / unverified (carried from the spike reports + new)
**New in this round**
- Qt6Nfc and the Qt6Multimedia core are still eager (in the trim eager list), even with keycard and FFmpeg lazy. Find who pulls them (DT_NEEDED from libStatus/StatusQ or a JNI carrier).
- The window-surface saving is lost after the first Browser visit (−4 EGL / −20 GPU gone); a flip back on Browser close is untried.
- Settings/Profile first open is +34 ms median (noise-level, one outlier); recheck with more n.

**From the spike reports**
- **win**
  - Native views other than WebView/TextureView are invisible until the flip.
  - Gesture-exclusion rects vs back gesture not fully checked.
  - Library activities become software-rendered.
  - Portrait IME on Lenovo not checked.
- **qr**
  - CameraX cold first open is +0.9 s on the Redmi (prewarm `ProcessCameraProvider`).
  - Real-camera decode is pending (only the injected frame was decoded). This smoke shows a live frame, but no QR was decoded.
  - Rotation of a running scanner not exercised.
- **media**
  - First play of each UI sound is +0.45 s on the Redmi (SoundPool decode; preload at AppMain).
  - Video playback (StatusVideo / collectible video) not exercised on a device.
  - Any new QtMultimedia user must go through `MediaHost` / `MediaBackend.ensureLoaded()`.
- **ssl**
  - Not checked on a device:
    - collectibles images;
    - link previews and community images;
    - keycard flows (lazy dlopen);
    - https video through FFmpeg;
    - save-image and copy-image (these still load OpenSSL on use).
  - PlatformHttps limits: the whole body is buffered, there is no POST, and cross-protocol redirects are not followed.
- **login**
  - Each by-URL popup has not been opened through the UI on a device (the probe loaded all 37).
  - Keycard drawer not tested with real NFC.
- **cheap**
  - The CJK fallback probe is unresolved.
  - The bundled triangles are not checked visually on iOS/desktop.
  - The runtime-compile check is not wired into CI (`STATUS_FAIL_ON_QML_SOURCE_COMPILE`).
- **order**
  - All numbers are warm; cold-cache runs are not done.
  - Order files go stale and must be regenerated per release (libStatus.order is 12 MB).
  - CI wiring of the order files is not done.
- **e2 kit:** https media through the rebuilt FFmpeg is untested.

## Branch
`git log e1f906462e..spike/floor-final`: 20 cherry-picks (-S, messages kept) + 27d06db05e test fix. Measurement-only branches (not for merge):
- `spike/floor-final-mark` = final + 0468f51f17
- `spike/floor-r3-base-mark` = base + 0468f51f17

APKs: final/apk/{base,final}.apk.
