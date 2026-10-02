# Translations: 4MB of `.qm` in the binary, the active one copied to the heap

Context: status-im/status-app#21528. Six `.qm` files (4.0MB) are embedded in the app library's rodata; the active
language is loaded from `qrc:` and copied into the heap (~0.9MB for the large languages), the others cost APK size
only. Small, but persistent and cheap.

## What to build

Load the active `.qm` from a file path (data dir or platform asset) so `QTranslator` maps it instead of copying;
ship translations outside the native library (Android assets, iOS bundle) so they stop living in rodata; consider
downloading rarely used languages on demand.

## Acceptance criteria

- [ ] `QTranslator` heap allocation at startup < 100KB (heapprofd)
- [ ] Language switch still works at runtime; storybook unaffected
- [ ] APK/IPA size delta recorded

## Blocked by

Nothing.
