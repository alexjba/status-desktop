# nomm: QtMultimedia + FFmpeg out of the Android app (#21528)

Status: FINAL. Branch `spike/floor-no-qtmultimedia` (worktree ~/Repos/status-desktop.worktrees/spike-floor-nomm), 2 signed commits on 51c123c2cf (draft PR #23106 tip). Not pushed. Work dir spikes-floor/nomm/. Package app.status.fnomm (both arms, same profile).

## Commits
1. `63f15ebc22` feat(android): show the still of a video collectible, open the video externally. `StatusRoundedMedia.videoPlaybackEnabled` (false on Android): video → fallback image, click → `videoClicked`; `CollectibleDetailView` then calls `Qt.openUrlExternally` instead of `StatusVideoModal`. No memory effect on its own.
2. `8ac9357013` perf(android): drop QtMultimedia and FFmpeg from the Android build (tree = the measured nomm3 APK).
   - `Status.pro`: `QT += multimedia` only on iOS. Android qrc copies (in `$$OUT_PWD/qrc-android/`, same basename so `Q_INIT_RESOURCE(resources)` still links) drop QML importing QtMultimedia: StatusVideo, StatusVideoModal, StatusQrCodeCapture, StatusQtSoundEffect (`mobile/scripts/android/drop_qml_importing.py`).
   - `StatusQrCodeScanner` loads `StatusQrCodeCapture.qml` by URL off Android (a type reference broke the QR dialog load on Android, see below).
   - StatusQ: `Qt::Multimedia` found/linked only off Android; `AudioUtils` computes QAudio's log→linear volume locally (max diff vs QAudio 4e-7 over 0..1.1, checked on host); `MediaBackend`/`MediaHost` (FFmpeg lazy-load) removed; `NativeSound` kept (`mediabackend.*` → `nativesound.*`).
   - Nim: `qtcamera` (NimQtCamera, links libQt6Multimedia) not imported on Android; new StatusQ `NativeCameraAccess` (Android-only, QtCore `QCameraPermission` + app-settings intent) installed as the `nimQtCameraBackend` context property that NimQtCamera's `CameraAccess.qml` reads; `qr_camera_frames.nim` uses only the native frame source on Android; `QRCodeScanner.qml` creates `FrameTap` in a Loader, off Android only.
   - Deploy: deny `libQt6Multimedia*`, `libplugins_multimedia_*`, `libqml_QtMultimedia_*`, `libQt6FFmpegStub-*`, libav*/libsw*, `Qt6AndroidMultimedia.jar` (trim script now also drops denied jars); `qt-deploy-lazy.txt` and its trim handling removed. trim's DT_NEEDED guard passes: no kept lib needs a multimedia lib.

## Results (Redmi A5, pr release made debuggable, prebuilt Qt 6.11.0 kit, same flags/package, n=3 alternating, medians)
MB; TOTAL = PSS+SwapPss.

| stage | arm | TOTAL | PSS | native | .apk | .so | EGL |
|---|---|---:|---:|---:|---:|---:|---:|
| login (37 s) | base 51c123c2cf | 174.0 | 173.3 | 53.2 | 66.7 | 17.1 | 7.6 |
| | nomm | 171.0 | 170.4 | 53.1 | 65.5 | 16.6 | 7.6 |
| | Δ | **−3.0** | −2.9 | −0.1 | **−1.2** | −0.5 | 0 |
| post (+120 s, wallet) | base | 243.0 | 240.0 | 94.8 | 72.9 | 17.6 | 8.5 |
| | nomm | 237.6 | 237.3 | 95.1 | 71.8 | 16.3 | 8.5 |
| | Δ | **−5.4** | −2.7 | +0.3 | **−1.1** | −1.3 | 0 |

- Per-library residency in base (smaps via run-as, midpoint apk attribution, every run): only **libQt6Multimedia is mapped: 932 KB Pss at login and post**, because libStatusPR, libStatusQ and libnim_status_client DT_NEED it. FFmpeg, its plugin, the stubs, MultimediaQuick and the QML plugin were already lazy (0 resident). No system lib mapped only in base (path-set diff of smaps). → **Solid saving ≈1 MB** (.apk −1.1/−1.2 matches 0.93 MB + relro). TOTAL −3.0/−5.4 is mostly noise (run 3 of nomm was +3/+9 above its own other runs; .so bimodal).
- APK: 258.9 → 239.6 MB (**−19.3 MB**); lib/ stored 253.2 → 234.0 MB (−19.2, 91 → 80 libs, multimedia set = 19.1 MB); lib/ zip -9 98.3 → 89.1 MB (−9.1). Dex unchanged in nomm1; nomm3 also drops Qt6AndroidMultimedia.jar (not measured separately).
- Cold start (am start TotalTime): 2289 → 2298 ms (ranges 2218–2328 / 2237–2312): same.
- Idle CPU: login screen 20 s 43.45 → 43.10 %; post-login 60 s 1.93 → 1.92 %: same. No CPU regression.
- Raw: nomm/dev/redmi/{rows,time,gpu}.csv, runs/{base1,nomm3}/ (meminfo -a, smaps, screenshots). `python3 nomm/summ.py nomm/dev/redmi base1 nomm3`.
- Measured: all numbers above. Not measured: ENTER→wallet (no marker commit on this base; WARN "start section not wallet" in every run of both arms, wallet tapped and checked by a11y).

## Functional checks
- Lenovo smoke nomm3 (dev/lenovo/smoke-1714): QR scanner dialog opens, CameraX ImageAnalysis use case active, camera OPENED, live preview + torch (qr-torch.png); camera permission reported granted by NativeCameraAccess. UI sound: Settings → Notifications volume slider → `SoundPlayer: playing sound 1`, SoundPool players in dumpsys audio. No TypeError/ReferenceError/"unavailable"/FATAL/UnsatisfiedLink.
- Lenovo smoke base1 (dev/lenovo/smoke-1718), same steps: QR camera opens the same way; no errors; no `SoundPlayer` line in that run (slider swipe likely missed; base unchanged code, sound already proven in the media/final reports).
- **Popups.qml:207 `open` of null (lead's question):** was on the first nomm build (nomm1, smoke-1649): `QRCodeScannerDialog` failed to load because `StatusQrCodeScanner` named `StatusQrCodeCapture` as a type (inside an unused Component) and the file was dropped from the Android qrc. Caused by my change; fixed (load by URL) before the A/B; nomm3 and base1 smokes show neither it nor the `dappsModel of null` TypeError (that one was a knock-on of the failed dialog flow: the smoke left the app).
- QR decode of an actual code on device: not done (no code in front of the camera); the Android frame path (NativeQrCodeCapture → statusq_qrcam → nim-qrscan) is unchanged except dropping the `> nativeSourceBase` check, since all sources are native on Android.
- Video collectible: test profile has none. Verified by `storybook/qmlTests/tests/tst_StatusRoundedMedia.qml` (new, 2 tests: still shown + videoClicked with the video URL; image path unchanged) and code reading. RED on base (property missing), GREEN on branch.
- Desktop: `tst_QRCodeScanner` 22/22 with the branch QML (base 22/22); the first Instantiator variant of the FrameTap gate failed 16 and was replaced by a Loader. Desktop/iOS keep inline video, Qt camera, SoundEffect. iOS and desktop builds not built here (risk: StatusQ CMake/Status.pro edits are Android-conditional; desktop StatusQ still finds/links Multimedia).

## Risks / follow-ups
- `NativeCameraAccess` impersonates NimQtCamera's `nimQtCameraBackend` context property (only CameraAccess's 2 properties + 3 slots). If CameraAccess.qml grows, Android needs the same. Cleaner long-term: a permission-only build of nim-qtcamera (submodule change) or a StatusQ CameraAccess type.
- qmlimportscanner still sees `import QtMultimedia` (desktop/iOS QML in the module dirs), so androiddeployqt deploys it and trim deletes it; a QtMultimedia qmldir stays in android_rcc_bundle (few KB).
- Video collectibles on Android open in an external app/browser; no inline playback (decided behaviour).
- Desktop/iOS build not verified in this spike.
