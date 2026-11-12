# Android UI-process memory floor: outcome (status-im/status-app#21528 / #22657)

Redmi A5, release-like builds (non-debuggable, R8), TOTAL PSS + SwapPss, n=5 per arm alternating, medians. Full data: final-report.md.

## Result (spike/floor-final, 21 signed commits on spike/floor-r3-base, not pushed)
| stage | master (start of work) | r3-base | **final** | final vs master |
|---|---:|---:|---:|---:|
| login screen | 234 | 189.6 | **152.7** | **−81** |
| post-login (+120 s, wallet) | 324 | 286.9 | **244.9** | **−79** |
| after chat/communities/settings | – | 331.1 | 282.3 | |
| after a Browser visit | – | 448.6 | 415.9 | |
GPU memory (outside PSS) −19 at login/post. Speed: start −0.46 s, ENTER→wallet −0.2 s, chat first open −0.1 s, idle CPU same. **150 at login: 2.7 MB short.**

## What's in it (each lever measured on its own; branch → report)
| lever | login / post (own A/B) | branch |
|---|---:|---|
| Build/link (RELR, libs trim, Basic style, -Oz…) + QML URL loading + qrc stub + no atlas | −42 / −39 (vs master) | spike/floor-stack-nodefer |
| QML slim + type closure | ≈ −2 / −1 (native −4) | spike/floor-r3-base |
| Custom Qt kit e2 (RELR, -Bsymbolic, ICF, thin-LTO -Oz) + hot-first order files for libStatus/libnim/StatusQ | −12.8 TOTAL; .apk −12.9 / −12.6 | spike/floor-order |
| Rare popups/views/onboarding pages by URL, keycard drawer on demand | native −2.2 / −2.6 | spike/floor-login |
| MIME by suffix, no source-compiled QML (+check), bundled ▴▾ font, dialogs on first open | native −2.9 / −4.7; dialogs −2.5 login | spike/floor-cheap |
| Media stack on first use + UI sounds via SoundPool + native CameraX QR scanner | −9.2 / −3.8 (+ survives sounds and scans) | spike/floor-media, spike/floor-qr |
| No HWUI buffers behind Qt's SurfaceView (Window.takeSurface) | −8.0 / −14.0, GPU −19 (lost after Browser) | spike/floor-win |
| No OpenSSL in the UI process (http loopback media server, keycard lazy, platform TLS for remote https) | −10.8 / −8.6 | spike/floor-ssl |
Stacked sum expected −41 / −47 vs measured −37 / −42 (≈2 MB overlap between media and ssl).

## To reach 150 and beyond (not built)
Property-cache sharing Qt patch (deferred, ≈ −7, risk in core lookup) · Qt6Nfc + Qt6Multimedia core still eager (≈ −0.5..1) · CJK font probe at login (−0.6 / −1.3, culprit not found) · qmlcachegen Storage vtables patch (−0.6) · window-surface flip back after Browser closes.

## Must verify before shipping
Real-camera QR decode (only injected frames decoded); rotation of a running scanner; video playback (collectibles); collectibles / link-preview / community images over platform TLS; keycard flows (lazy dlopen + drawer with real NFC); save/copy image; each by-URL popup opened through the UI; native views other than WebView with the taken window surface; gesture-exclusion vs back gesture; bundled ▴▾ font on iOS/desktop; status-go owners' OK for plain-HTTP loopback; Basic-style visual sweep; ENS/syncing flows (StateMachine removal). Known regressions to fix: CameraX cold first open +0.9 s (prewarm), first UI sound +0.45 s (preload). Order files: generate in CI (libStatus.order 12 MB).

## Dropped (measured)
Vulkan · native pre-Qt login · MADV_DONTNEED / MADV_RANDOM · android media backend · trimComponentCache · defer AppMain precompile · sections compiled on first open · bytecode-only qmlcache · depth/stencil off · GC/purge after login · native text · allocator knobs · Qt order files (LTO-renamed symbols) · loading hot chat/community/home types by URL (>30 ms first opens).

---
# Earlier version (wave 2)
# Android UI-process memory floor: measured path (status-im/status-app#21528 / #22657)

Device Redmi A5 (Mali-G57, Android 15). Release-like builds (non-debuggable, R8). MB = TOTAL PSS + TOTAL SwapPss from `dumpsys meminfo`. Debug builds read 14–26 MB higher, almost all of it Java-runtime overhead.
Target per owner: cut the **fixed floor**, i.e. memory present before login that persists after login. Login-only savings and accounting tricks don't count.

## Where we are (measured, release-like, Redmi, n=2–3)

| | login | post-login (+120 s) |
|---|---:|---:|
| master | 234 | 324 |
| stacked levers, fixed-floor basis (spike/floor-stack-nodefer b9baeddfb1) | **192 (−42)** | **285 (−39)** |
| startup / login→wallet / idle CPU | −0.9 s / same / same | |

## Levers

### Measured and in the stack above
| lever | branch / commit | login | post | speed | notes |
|---|---|---:|---:|---|---|
| Build/link: RELR, rtti_off, libnim exports, Widgets out, qt_libs 62→18 + Basic style, StateMachine/labs/Core5Compat out, CA dedupe, `-Oz` | spike/floor-build (minus 1ae29d4318 media) | −16..−21 | −16..−28 | idle same | Basic-style visual sweep of 12 files; ENS/syncing flows untested |
| QML: never-opened views/popups loaded by URL + section prefetch | spike/floor-qml 085d315d40 | −15 native | −12 native | login→wallet −0.3 s; first opens ≈ base | round-2 views not all opened on the device |
| qrc QML source stub | spike/floor-pages 8de0ce0374 | −2.6 | −2.6 | — | needs a CI "no compile-from-source" check (see runtime-compile finding below) |
| No texture atlas | spike/floor-gfx f1deefbeee | GPU −9 | GPU −10 | CPU same | GPU memory is system RAM but outside PSS; GPU frame time + Adreno unchecked |

### Measured, not yet in the stack
| lever | branch / commit | login | post | speed | notes |
|---|---|---:|---:|---|---|
| Enhanced Qt kit (RELR, -Bsymbolic-functions, ICF, thin-LTO -Oz), built in the Android Docker image | spike/floor-qt-docker bd209b5fa6 (+9394cfc11c fdroid flags fix) | −7.2 | −6.0 | first frame −50..70 ms, CPU same | ffmpeg https fixed in 1fa37c166c (Qt FFmpegStub-ssl/crypto scheme, matches the prebuilt kit; https video on a device and the Docker run of this commit untested); image ~150–180 MB smaller; new tag 20261106-qt6.11.0-android |
| QML slim: single-orientation StatusSectionLayout, Profile subviews by URL, browser menus/wallet skeletons on demand, onboarding dialog | spike/floor-qmlslim (caf7aa1a78…a0a1b9589f) | −1.6 native | −2.7 native; **−14.6 after visiting sections** | **login→wallet −0.72 s, Browser first open −0.78 s**, no section slower | Profile +47 ms on Lenovo landscape only; 66/66 layout tests pass; rotation keeps state |
| QML type closure: Constants groups → JS objects, 5 more views by URL | spike/floor-qmlmeta 6848e2e1d9 | ≈ −1.2 (est from Mac) | ≈ −1.2 | — | Mac only |
| Message-row pool built on first chat section (owned by the row-pool workstream) | spike/floor-qmlslim 3fe9470399 | 0 | **−32..−41** | defers ~54 row builds to first chat open: **up to 2.7 s UI block on the Mac with messages** (device cost unmeasured) | hand to the row-pool owner; latency must be solved first |

QML slim caveats: two rotation tests in tst_StatusSectionLayout were rewritten to the new behaviour, so review them. Full storybook suite and `make run-statusq-tests` not run. Mobile browser menus and the SSL dialog not opened on a device.

### Cheap, identified, not built
| lever | est. login / post | source |
|---|---:|---|
| Find the QML units compiled from source at runtime (`QmlUnitGenerator` 2.8 MB) and get them into qmlcache | −0..1 / −2..4, also CPU | stack heapprofd |
| StatusQ `QMimeDatabase::mimeTypeForFile` → extension match | −1.8 / −1.8 | stack heapprofd |
| Font fallback walk: one 12 px glyph missing from Inter loads 108 system fonts | 0 / −2..4 | plat |
| More never-instantiated types loaded by URL (top 35 boundary roots) | −4..5 / −4..5 | qmlmeta (Mac) |

### Dropped (measured)
Vulkan (GPU +14..18 offsets PSS −16..20; breaks HW video decode) · native pre-Qt login screen (login-only, 0 after login) · page release MADV_DONTNEED (accounting) · MADV_RANDOM fault-around (0 warm; cold +754 ms login) · android media backend (breaks QR camera) · trimComponentCache (360 ms stall, ~1 MB) · defer2 / skip AppMain precompile (+0.54..1.45 s login) · Profile/sections compiled on first open (L6: +73..124 ms) · chat skeleton on entry (L5: +190 ms) · bytecode-only qmlcache (+13 % CPU) · depth/stencil off · GC/purge/trim after login · native text rendering · allocator knobs.
Deferred: property-cache sharing Qt patch (#22676; variant A ≈ −2..3, risk in core QML lookup).

## Projection (release, fixed floor)
| | login | post-login |
|---|---:|---:|
| measured stack | 192 | 285 |
| + enhanced Qt + QML slim + qmlmeta | ≈ 182 | ≈ 275 (after visits −14 more) |
| + cheap identified levers | ≈ 175–180 | ≈ 265 |
| + row pool (other owner) | ≈ 175–180 | ≈ 225–235 |

## What's left at the login screen (192, release, w-stack attribution)
Fixed ~125: Qt lib pages 33–36, GPU-driver malloc ~13 (GLES context 8.3), EGL 12.3, system .so 11.5, ART ~10.5, Mali .so 7, Nim code 7, StatusQ 5, OpenSSL 5, misc.
Rest ≈ 24 native + pages = the QML type data and instances of the login/onboarding UI (estimate: heapprofd at the login screen failed on this device).

**150 at the login screen is not reached by any measured lever (≈ −25 short).** Next candidates, in order:
1. Lean login screen in QML: minimal imports and object tree, onboarding flows loaded only when used. Needs a login-screen heap capture first; partly login-only.
2. Qt lib pages (33–36) + our libs: order file (hot/cold code layout) on the custom kit and on libStatus/libnim/StatusQ. The Redmi runs mostly cold in normal use (2.6–4.5k major faults per relaunch vs 20 warm), so code locality matters for both memory and startup. libnim alone measured −4.8 at login on the Lenovo; post-login is unmeasured.
3. GPU: the 8.3 MB GLES context and 12 MB of swapchain are fixed while we stay on GLES.

## Speed results worth shipping on their own
Login→wallet −0.72 s (single-orientation layouts), Browser first open −0.78 s, app start −0.9 s (stack), first frame −50..70 ms (Qt kit). Still slow on the Redmi: chat first open 1.4 s, Browser 1.0 s.
