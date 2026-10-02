# Fonts: 23 font files copied into the heap at startup, 5 weights ever used

Context: status-im/status-app#21528. `StatusQ/Core/Theme/Fonts.qml` declares 23 `FontLoader`s (Inter 8 weights,
InterStatus 9 incl. an italic, RobotoMono 6). Each `FontLoader.source` on a `qrc:` URL runs
`QFontDatabase::addApplicationFont` → `QIODevice::readAll`, so all 23 files (5.3MB) are copied into QByteArrays for
the app lifetime (7.1MB live at the login screen, heapprofd), and FreeType copies the bytes again per face it opens
(`QFreetypeFace::getFace` → `readAll`, 1.3MB and growing). The file pages are touched too (5.2MB resident).

QML requests only `Font.Medium` (158 sites), `Font.Bold` (80), `Font.Normal` (24), `Font.DemiBold` (18, no file
exists for Inter/InterStatus, falls back today) and `Font.Light` (3). Thin, ExtraLight, ExtraBold, Black and the
italic are never requested.

## What to build

1. Ship and load only the weights in use: Inter and InterStatus Light/Regular/Medium/Bold (+ SemiBold if design
   wants DemiBold rendered rather than synthesized), RobotoMono Regular/Medium/Bold. Remove the rest from the qrc.
2. Load from a real file instead of `readAll`: on first start copy the font files to the app data dir (or ship them
   as Android assets / iOS bundle resources) and point `FontLoader.source` at the file URL, so FreeType maps the
   file and no heap copy is kept. Verify with heapprofd that `addApplicationFont` no longer allocates the file size.
3. Keep `Fonts.qml` API unchanged (`baseFont`, `monoFont`, `codeFont`).

## Acceptance criteria

- [ ] Ledger rows: UI PSS at login screen and settle before/after; heapprofd shows < 1MB under `addApplicationFont`
- [ ] Storybook screenshot suite unchanged for every weight in use; DemiBold rendering decision recorded
- [ ] APK/IPA size delta recorded
- [ ] Desktop uses the same loader change (no divergence)

## Blocked by

Nothing.
