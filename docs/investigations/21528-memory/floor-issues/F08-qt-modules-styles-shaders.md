# Qt modules, Controls styles and runtime shader compilation loaded without being used

Context: status-im/status-app#21528. Page-table residency of the UI process at the login screen (Redmi A5,
2026-10-02): 129 Qt libraries/plugins are packaged (91MB of APK), 48.6MB resident, and code pages grow only ~4MB after
login. 27MB is the core set (Core, Gui, Qml, Quick, Network, Svg, QmlModels, QuickTemplates2, Controls2 Basic+Impl,
Layouts, Shapes, Effects). The rest is loaded or packaged without a consumer:

| group | resident MB | APK MB | why |
|---|---|---|---|
| Controls2 styles: Material 1.9 (Android's default style, no file imports it), Universal 1.0 (12 files + `mobile/scripts/gen_controls_shim.sh … Universal`), Fusion 0.8, Imagine 0.7, FluentWinUI3 0.2 + 3.2 plugin | 4.6 | 12.5 | `import QtQuick.Controls` makes androiddeployqt bundle every style; Qt loads Material at engine start |
| Multimedia + MultimediaQuick + android and ffmpeg plugins + libavcodec/avformat/avutil/swscale | 3.9 | 17 | 4 QML files (`StatusSoundEffect`, `StatusQrCodeCapture`, `StatusVideo`, `StatusVideoModal`); plugins listed in `load_local_libs` load at start |
| Widgets | 2.4 | 6.7 | `QT += widgets` in `mobile/wrapperApp/Status.pro`; no C++ consumer, app is a `QGuiApplication` |
| QuickDialogs2 + QuickImpl | 1.3 | 3.2 | `StatusFileDialog`, `StatusFolderDialog`, `StatusSaveFileDialog` |
| ShaderTools | 1.0 | 4.1 | 5 `ShaderEffect`s (`StatusRoundedComponent`, `LoadingComponent`, `StickySendModalHeader`, `SendModalFooter`, `ConfirmChangePasswordModal`) without `.qsb`: shaders compile at runtime on every start |
| VirtualKeyboard + input plugin, StateMachine (2 files), Labs platform/qmlmodels/folderlistmodel, Lottie + VectorImage generators, Quick3DUtils, tiff/ico/tga/wbmp/icns image formats | ~2.5 | ~7 | transitive defaults |
| qmltooling ×9, offscreen/minimal platform plugins, android style plugin | 0 | 1.2 | packaged by default |

Qt5Compat GraphicalEffects itself costs 1.1MB of file pages; its memory cost is GPU-side and tracked separately (F13).

## What to build

- Set the Controls style explicitly before the engine starts (Basic, or the shim's style) so Material is never loaded;
  drop the 12 explicit `QtQuick.Controls.Universal` imports if the shim covers them; exclude the other style plugins from
  the Android/iOS package (deployment settings / `QT_QML_IMPORT_...` filters).
- Remove `widgets` from the mobile link; verify no `QApplication` need.
- Precompile the 5 shaders with `qsb` for GLES/Metal/Vulkan and reference the `.qsb`; confirm ShaderTools is absent
  from the process map.
- Multimedia: remove the plugins from `load_local_libs` so they load on first `MediaPlayer`/`Camera`; evaluate
  shipping only the Android media backend (ffmpeg backend is 16MB of APK).
- Dialogs: use native pickers on mobile or load the dialog module on first use.
- Unlink VirtualKeyboard, StateMachine, Labs, Lottie/VectorImage, Quick3DUtils; exclude qmltooling and spare
  platform plugins and the unused image formats from release packages.

Expected: ~16MB resident at the login screen (≈8–10 of really touched pages once fault-around is discounted), ~50MB
of APK, and no runtime shader compilation.

## Acceptance criteria

- [ ] Per change: the library is absent from the process map (or loads only on use), UI PSS rows before/after at login screen and settle, warm and cold
- [ ] No runtime shader compilation; storybook screenshots identical for the 5 effects
- [ ] APK/IPA size before/after
- [ ] Video, QR capture, sound effects, file pickers and the Browser still work on device

## Blocked by

Nothing.
