# Qt modules, Controls styles and runtime shader compilation loaded without being used

Context: status-im/status-app#21528. At the login screen (page-table residency, Redmi A5): QtQuickControls2
styles Material 1.6MB, Universal 1.0, Fusion 0.4, Imagine, Basic 0.7 are all packaged and Material/Universal are
loaded (Material through `import QtQuick.Controls.Material` in `FilterComboBox.qml` and `StatusCheckBox.qml`);
`QtWidgets` is linked (`QT += widgets`, 2.4MB resident, 11% used) although the app is a `QGuiApplication`;
`QtVirtualKeyboard`, `QtQuickDialogs2`, `QtMultimedia` + ffmpeg (`libavcodec` 11.8MB file, loaded at start through
`load_local_libs`) are loaded before any use; `libQt6ShaderTools` (4.1MB) is loaded and page-faults during startup
because the 5 `ShaderEffect` users (`StatusRoundedComponent`, `LoadingComponent`, `StickySendModalHeader`,
`SendModalFooter`, `ConfirmChangePasswordModal`) ship no precompiled `.qsb`, so shaders are compiled at runtime
(heap + CPU on every start).

## What to build

- Replace the two Material imports with StatusQ equivalents; remove unused style plugins from the APK/IPA.
- Drop `widgets` from the mobile link; check nothing on mobile needs `QApplication`.
- Precompile the 5 shaders with `qsb` (all RHI backends) and reference the `.qsb` files, so ShaderTools is not loaded.
- Load the multimedia/ffmpeg and webview plugins on first use (remove them from `load_local_libs`; Qt loads plugins
  lazily when the module is first instantiated) or defer `QtMultimedia` import to the views that use it.
- VirtualKeyboard: confirm it is unused on mobile (native IME) and unlink.

## Acceptance criteria

- [ ] Per change: libraries no longer present in the process map at the login screen; UI PSS rows before/after
- [ ] No runtime shader compilation (no `libQt6ShaderTools` in the map) with identical rendering in storybook screenshots
- [ ] APK/IPA size delta recorded

## Blocked by

Nothing.
