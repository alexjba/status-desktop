# Brief: remove QtMultimedia from the Android build (agent "nomm")

Read first: /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/COMMON.md. It applies in full, except where this brief overrides it.

## Goal
Take QtMultimedia out of the Android app entirely: QtMultimedia, MultimediaQuick, the QML plugin, the FFmpeg backend plugin and libav*/libsw*/FFmpegStub libs, plus the code that only exists to lazy-load them. Measure what it saves. Desktop and iOS keep QtMultimedia unchanged.

## Starting point (overrides COMMON's "fresh origin/master")
- Branch from the draft PR tip: `perf/21528-android-ui-memory-floor` at 51c123c2cf (status-im/status-app#23106). New branch `spike/floor-no-qtmultimedia`, new worktree `~/Repos/status-desktop.worktrees/spike-floor-nomm`. The tree to compare against (baseline) is 51c123c2cf built with the same flags and a different package.
- Commit 6 of that PR ("load the media stack on first use; native QR scanner") already moved sounds to SoundPool (StatusQ mediabackend.cpp) and QR scanning to CameraX (NativeQrCodeCapture → nim-qrscan via statusq_qrcam_*). FFmpeg is lazy (mobile/scripts/android/qt-deploy-lazy.txt, MediaBackend::ensureLoaded). libQt6Multimedia is probably still eager (Status.pro `QT += multimedia`).

## What still uses QtMultimedia (as found by the lead; verify)
- `StatusVideo.qml` (MediaPlayer/VideoOutput), used only via `StatusRoundedMedia` → `CollectibleMedia`: the collectible detail view main media and the send-collectible sign modal (`SendSignModal.qml:186`). `StatusVideoModal` via `Global.openVideoPopup`, only from `CollectibleDetailView.qml:374`. Collectible grid tiles never pass a video mediaType.
- `StatusQrCodeCapture.qml` (desktop/iOS camera path) and `StatusQtSoundEffect.qml` (desktop/iOS sound path).
- Anything else you find (grep `QtMultimedia`, `MediaPlayer`, `VideoOutput`, `SoundEffect`, `CaptureSession`, C++ `QMediaPlayer`/`QtMultimedia` includes, Nim `qtcamera`/NimQtCamera, the frame tap in src/app/global/qr_camera_frames.nim).

## Decided behaviour
On Android, video collectibles show the still image (`fallbackImageUrl` / the collectible image) and do not play inline; tapping opens the video URL externally (Qt.openUrlExternally) instead of StatusVideoModal. Keep the change small and confined to the mobile/Android path (e.g. a platform check in StatusRoundedMedia / the openVideoPopup call site). Desktop and iOS keep inline playback.

## Constraints
- QML files that import QtMultimedia must not be loaded (and ideally not AOT-compiled or bundled) on Android; the build must not need the module. Check qmlcachegen/qmllint and the StatusQ CMake on Android.
- Remove from the Android deploy: the multimedia plugins, FFmpeg libs, `qt-deploy-lazy.txt` entries, `MediaBackend` lazy-load plumbing that only served FFmpeg (keep the SoundPool path), `QT += multimedia` for android in Status.pro, StatusQ's Qt::Multimedia link on Android.
- Check whether NimQtCamera (qtcamera) links QtMultimedia on Android; if the Android QR path no longer needs it, drop it there.
- No CPU regressions. Confirm sounds still play (SoundPool), the QR scanner still opens and decodes (CameraX), and a video collectible shows its image (if the test profile has none, verify by reading code and with a QML test or storybook, and say so).
- One commit per logical lever (e.g. 1: collectible video fallback on Android, 2: drop QtMultimedia from the Android build). Commit messages state measured deltas.

## Measurement (what the owner wants to know)
- Redmi A5, A/B baseline (51c123c2cf) vs your tip, same build type and flags, own packages (e.g. app.status.fnommb / app.status.fnomm). Medians of ≥3 runs at the login screen and after login (+120 s, wallet): TOTAL PSS, native heap, .so, .apk mmap, EGL. TOTAL is noisy; also report per-library residency (smaps) for libQt6Multimedia*, multimedia plugins and libav* in the baseline, which is the direct evidence of what goes away.
- APK size delta (compressed and uncompressed lib/ bytes).
- Startup: cold start to login screen time, A/B, medians of ≥3.
- Idle CPU over 60 s post-login, A/B.
- Mark measured vs estimated honestly.

## Deliverable
- Branch + commits in your worktree. NO pushes, NO PRs, NO GitHub comments.
- Report: /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/45f4f7fe-70e3-435a-8371-d63aba0c0749/scratchpad/spikes-floor/nomm-report.md, and write `nomm.final` next to it when done.
- Final message: ≤10-line summary with the memory numbers + report path.
