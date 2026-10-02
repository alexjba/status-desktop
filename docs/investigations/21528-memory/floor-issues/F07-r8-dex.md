# Android: 9MB of dex mapped in both processes, no R8

**Expected gain:** memory and APK. Dex 9MB + 5 secondary; UI process maps ~21MB of dex pages (~11MB PSS, shared with the service). R8 typically halves the dex: ≈−4..−6MB PSS over the two processes, −5..−8MB APK. Estimate until built; clean pages, so cold-cache gain is smaller than warm.

Context: status-im/status-app#21528. `classes.dex` is 9.0MB plus 5 secondary dex files; `minifyEnabled = false`
in `mobile/android/qt6/build.gradle` for release. At the login screen the UI process has 21.4MB of dex pages mapped
(the service ~12MB), counted as PSS shared between the two processes (~11MB PSS in the UI process). Dex pages are
clean file pages but the ART runtime also keeps per-class metadata and the JIT cache (4MB) proportional to loaded
classes.

## What to build

Enable R8 (`minifyEnabled true`, `shrinkResources true`) for the release and profile variants with keep rules for
Qt's Java bindings (`org.qtproject.qt.android.**`), our AIDL/IPC classes, and reflection used by push notifications;
verify on device that the UI, the service process, notifications and the share extension all work; measure dex size
and PSS.

## Acceptance criteria

- [ ] `classes*.dex` total before/after; UI and service PSS at login screen and settle before/after
- [ ] Keep rules checked in with a comment per rule naming the reflective use
- [ ] E2E smoke on device: login, chat, wallet, notification tap, share-to-Status

## Blocked by

Nothing.
