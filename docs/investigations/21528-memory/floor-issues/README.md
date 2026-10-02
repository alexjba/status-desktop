# Pre-login floor — issue drafts (local, not yet on GitHub)

All are children of status-im/status-app#21528 once agreed. Numbers: Redmi A5, `floor.md`. Every issue carries the
same measurement contract: PSS of both processes at the login screen (warm and cold page cache) and at the wallet
settle point, split into unreclaimable (anon + dirty file) and clean file pages, plus CPU time to the login screen and
login duration; before/after rows in `ledger/`.

| # | title | kind | est. persistent MB |
|---|---|---|---|
| F01 | Go heap before login in the service process | measure | 0–15 |
| F02 | QML property-cache sharing between types (Qt prototype) | prototype | 15–25 |
| F03 | Scene-graph atlas and glyph-cache budget | measure | 5–15 |
| F04 | Go binary diet, level 1: dead code and build config | implement | 10–15 |
| F05 | Go binary diet, level 2: sized feature-removal list | analysis → sign-off | up to ~10 more |
| F06 | Fonts: used weights only, mapped not copied | implement | 8–11 |
| F07 | R8 on the Android dex | implement | 4–6 |
| F08 | Qt modules, styles, package exclusions and runtime shader compilation | implement | 8–10 (+~50MB APK) |
| F09 | Translations: map the active `.qm`, keep the rest out of the binary | implement | ~1 (+APK) |
| F10 | QML feature modules loaded by URL and trimmed on close (pilot: popups) | convention + pilot | 10–20 post-login |
| F11 | Settings subpages and sections by URL | follow-up of F10 | part of F10's range |
| F12 | Floor ledger tooling: pagemap residency, APK attribution, cold-cache rows | tooling | — |
| F13 | Qt5Compat effects: layered items and GPU driver host memory | measure → implement | up to ~28 (Mali growth after login) |
