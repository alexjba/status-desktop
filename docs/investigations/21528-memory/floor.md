# Pre-login floor (Android)

Issue: https://github.com/status-im/status-app/issues/21528. Scope decided 2026-10-02: the floor is the footprint at
the login screen (no profile loaded); only components that **persist after login** count; two processes stay and each
shrinks; any toolchain tier is allowed (build config, Qt configure, Qt source patches after a prototype). Target
−150MB committed, −200 stretch, measured as total PSS on the Redmi A5, with the unreclaimable and clean-file parts
reported separately and every fix re-checked at settle and with a cold page cache.

Raw data: `profiles/floor/`. Scripts: `mobile/scripts/profiling/floor/` (`pmscan.c` reads page-present bits for a
mapping through `/proc/pid/pagemap` inside the app uid, `apkattr.py`/`pmattr.py` attribute APK-backed pages to
libraries, ELF sections and embedded Qt resources, `smapsagg.py` aggregates smaps, `qmlreach.py` computes the static
QML type-reference closure, `pagescan.c` checks page-cache residency of a file).

## Composition at the login screen (Redmi A5, PSS MB)

Release build (3 runs) and the debuggable build (needed for pagemap/heapprofd; code pages ~15% larger):

| bucket | UI release | UI debug | service release | service debug | note |
|---|---|---|---|---|---|
| native heap (malloc) | 99 | 99 | 2 | 2 | |
| code + rodata file pages (`.apk`/`.so` mmap) | 105 | 125 | 50 | 61 | libs load straight from the APK |
| Go runtime arenas (`Unknown`) | 12 | 12 | 27 | 29 | |
| EGL mtrack | 16 | 16 | 0 | 0 | window buffers |
| Dalvik/ART/jit/framework | ~20 | ~25 | ~10 | ~12 | per-process baseline |
| **total** | **254–272** | **285–310** | **79–102** | **117** | release pair ≈ 330–360 |

### UI process, file pages by library (debug, resident MB of `base.apk`)

| MB | library | of which exec | note |
|---|---|---|---|
| 38.2 | libStatus (app) | 2.8 | `.rodata` 28.9 (see below), `.rela.dyn` 4.3, `.data.rel.ro` 1.8 |
| 23.4 | dex (`classes*.dex`), `resources.arsc` | | `minifyEnabled=false`; shared with the service |
| 7.6 | libnim_status_client | 5.5 | |
| 5.8 / 5.8 / 4.6 / 4.4 | Qt Gui / Quick / Core / Qml | 90–100% of text | fault-around inflated (below) |
| 4.3 | libcrypto | 2.2 | |
| 3.0 | libStatusQ | 1.5 | |
| 2.4 | Qt Widgets | 11% of text | linked, barely used |
| ~6 | Qt QuickTemplates2 + Controls2 styles (Material 1.6, Universal 1.0, Basic 0.7, Fusion 0.4, Imagine) | | Material loaded through two imports |
| ~4 | Qt Network, Multimedia, Svg, VirtualKeyboard, ShaderTools 1.0, QuickDialogs2, Qt5Compat effects | | |
| 1.0 / 0.6 | libavcodec / libavformat | 1% | ffmpeg plugin loaded at start, untouched |

App library `.rodata` (50.8MB in the file, 28.9 resident): Qt resources 8.7 (all 23 font files 5.2, QML sources 2.2,
one `.qm` 0.3, png/svg 0.8) and 20.2 outside any resource, almost entirely compiled QML units
(`QmlCacheGeneratedCode::*::qmlData`, 2496 units, 35.8MB; 2420 fully resident in 65 contiguous runs).

### Service process, file pages (debug)

libstatus.so 57.8 resident of 84: `.text` 19.0 (of 25.7), `.gopclntab` 13.3 (of 22.8), `.data.rel.ro` 8.9 (fully
dirtied by relocations, private), `.rela.dyn` 8.5, `.rodata` 6.3, `.noptrdata` 1.1. Go heap 20.1 + 2.3 immortal
metadata before any profile is opened. Dex share 11.8.

### Why "resident" overstates the touched set

Residency was read from the process page tables (pagemap), not from the page cache. Used Qt libraries show 90–100%
of their text resident while unused ones show 1–11% (libavcodec, Widgets): the kernel maps whole fault-around windows
around each real fault when the page cache is hot (fresh install + APK pulled over adb). The compiled QML units are
mapped the same way, not loaded by the engine: the static reference closure from `ui/main.qml` is 37 files, the
onboarding wrapper adds 173, plus singletons (`qmlreach.py`). These clean pages are dropped under memory pressure, so
the true floor on a loaded device is lower than dumpsys shows, and every fix must also be measured with a cold page
cache (`pagescan drop` has no effect while the APK is mapped; reboot or evict via a large file read).

## UI heap at the login screen (heapprofd, 98.9MB sampled, 3418 call sites)

| MB | cause | persists after login |
|---|---|---|
| 13.3 | `QQmlPropertyCache::copyAndReserve` under `QQmlTypeLoader::setCachedUnit`: every QML type, and every inner object with declared properties, copies its parent's full property table | yes, grows to ~25 (engine heap 33→49) |
| 8.4 | fonts: `FontLoader` → `QFontDatabase::addApplicationFont` → `QIODevice::readAll` for all 23 weights (7.1) + FreeType face copies (1.3) | yes |
| 4.1 | `QmlIR::QmlUnitGenerator::generate`: the in-memory unit regenerated for each loaded QML file | yes, grows |
| ~3 | imports/qmldir strings, `QQmlImports::addLibraryImport` | yes |
| 16.7 | Mali host-side: `eglCreateContext` 3.4, texture staging, per-frame | yes, 45 after login |
| 4.2 | onboarding image decoded at native size (`convertToFormat` under `StackView`) | no (onboarding unloads) |
| ~10 | QML runtime misc (bindings, strings, JS heap) | yes |

Service heap: 0.7MB native; the 20MB Go heap before login is unattributed (pprof starts only with the node in the
installed build; the profiling build now starts it in `mainProc`).

Fonts actually used by QML (`Font.*` references): Medium 158, Bold 80, Normal 24, DemiBold 18, Light 3, over three
families (`Fonts.baseFont` Inter, `monoFont` InterStatus, `codeFont` RobotoMono). Thin/ExtraLight/ExtraBold/Black
and InterStatus italic are never requested. Inter ships no DemiBold file, so those requests already fall back.

Other startup facts: libQt6ShaderTools faults during startup (5 `ShaderEffect` users with no `.qsb` in the repo →
runtime shader compilation); `QSG_RENDER_LOOP=threaded` is the only scene-graph setting (atlas/glyph caches at Qt
defaults); go-ethereum metrics are forced on in the Android link (`metrics.EnabledStr=true`); Android build tags
`gowaku_no_rln nowatchdog disable_torrent`; CI builds Qt from source (`ci/Jenkinsfile.qt-build`).

## Sizing of the persistent levers

| item | est. persistent saving | certainty |
|---|---|---|
| property-cache sharing between types (Qt patch) | 15–25 | prototype first |
| QML feature modules by URL + `trimComponentCache` on close | 10–20 post-login | design |
| fonts: used weights only, mmap instead of copy | 8–11 | sure |
| scene-graph atlas / glyph-cache budget | 5–15 | measure |
| Go binary diet (text, pclntab, relocs scale together) | 10–20 | needs package breakdown |
| Go pre-login heap (metrics registry, init tables) | 0–15 | measure |
| R8 on dex | 4–6 | sure |
| ShaderTools precompile, Material/Universal/Fusion/Imagine out, Widgets/VirtualKeyboard unlinked, ffmpeg lazy | 4–8 | sure |
| translations: mmap the loaded `.qm`, ship the rest outside rodata | ~1 heap, −4MB APK | sure |

Not reachable from the floor: Dalvik/ART baseline (~35 over two processes), EGL window buffers (16), relocated Go
data (8). Floor work alone yields ~60–120; the 150–200 target needs the post-login half of the same levers, which is
why URL-loaded modules are part of this wave.

Decisions (2026-10-02): keep two processes, no late start of the service; PSS is the contract with both columns
reported; all toolchain tiers allowed; URL-loaded modules become the QML convention (popups and settings subpages
first, sections second); Go: dead-code and build-config trimming now, feature removal only through a sized list with
product sign-off, and that list is a deliverable.

Issue drafts: `floor-issues/` (local until agreed for GitHub).

## Go library: code size by package (unstripped Android build, 26.7MB text; `profiles/floor/go-android-*`)

| MB text | package | why it is linked |
|---|---|---|
| 5.3 | status-go own code (protocol 1.1, wallet 0.9, communities 0.35, messaging 0.3, contracts 0.4, db 0.3, …) | |
| 1.0 | libp2p core | waku |
| 1.0 + 0.3 | pebble + goleveldb + go-ethereum rawdb/ethdb | `go-ethereum/node` ← `pkg/services/personal` |
| 0.9 | crypto (std) | |
| 0.84 | go-ethereum (rest) | |
| 0.55 + 0.52 | gogo/protobuf + google protobuf | libp2p-pubsub vs. our protobufs |
| 0.52 + 0.22 | quic-go + gojay | libp2p QUIC transport |
| 0.9 | pion webrtc/dtls/ice/sctp | libp2p WebRTC transport |
| 0.46 + 0.34 | go-waku + libp2p-pubsub | |
| 0.42 | extkeys | key derivation |
| 0.29 + 0.4 | opentelemetry + grpc | OTLP trace exporter ← `internal/protocol` |
| 0.29 | gofakeit | `internal/protocol/communities` (non-test) |
| 0.3 + 0.16 | testify + yaml.v3 | `internal/protocol` (non-test) |
| 0.28 | miekg/dns | multiaddr-dns ← go-waku |
| 0.27 | gnark-crypto bls12-381 | kzg4844 ← `signer/core/apitypes` ← `mobile` |
| 0.22 | brotli | `pkg/services/sharedurls` |
| 0.19 | x/image | `internal/images` |
| 0.18 | gofeed + json-iterator | RSS in `internal/protocol` |
| 0.12 ×3 | sentry, gojsonschema, validator | callog; go-wallet-sdk token lists |

Resident pages scale with text: text 19/25.7MB, pclntab 13.3/22.8, relocation data 8.9 + 8.5 at the login screen,
so every MB of text removed is worth ~2MB resident on the service process. Level 1 (dead code, `floor-issues/F04`)
and the level-2 feature list (`F05`) are built from this table.

## Qt/QML dependency cost (UI process, login screen, page-table residency; `profiles/floor/ui-login-apk-pages.txt`)

129 Qt libraries and plugins are packaged (91MB in the APK); 48.6MB of them is resident at the login screen, and the
release ledger shows code pages growing only ~4MB after login, so this is the dependency cost. Module heap is small
(post-login heapprofd: QuickTemplates2 0.5, QuickControls2 0.3, Layouts 0.2, QmlModels 0.2); the heap lives in
Qml/Core/Gui/Mali regardless of which modules are linked.

| group | resident MB | APK MB | pulled in by | verdict |
|---|---|---|---|---|
| Core, Gui, Qml, Quick, Network, Svg, QmlModels, QuickTemplates2, Controls2 Basic+Impl, Layouts, Shapes, Effects, platform plugin, c++_shared | 27 | 33 | the app | keep |
| Controls2 styles: Material 1.9 (loaded as Android's default style although no file imports it), Universal 1.0 (12 files + `gen_controls_shim.sh … Universal`), Fusion 0.8, Imagine 0.7, FluentWinUI3 0.2 (+3.2 plugin, untouched) | 4.6 | 12.5 | `import QtQuick.Controls` makes androiddeployqt bundle every style; the default style loads Material at engine start | set the style explicitly before the engine (Basic or our own), drop the 12 Universal imports, exclude the other style plugins from the package: −3.8 resident, −11 APK |
| Multimedia + MultimediaQuick + android & ffmpeg plugins + libav* | 3.9 | 17 | 4 QML files (`StatusSoundEffect`, `StatusQrCodeCapture`, `StatusVideo`, `StatusVideoModal`); the plugins are in `load_local_libs`, so they load at start | lazy: load the plugins on first use; consider the Android media plugin only (ffmpeg is a second backend, 16MB): −3.5 resident, −16 APK |
| Widgets | 2.4 | 6.7 | `QT += widgets` (no C++ consumer found; app is `QGuiApplication`) | unlink: −2.4 resident, −6.7 APK |
| QuickDialogs2 + QuickImpl + Utils | 1.3 | 3.2 | 3 StatusQ file dialogs | lazy or native pickers on mobile: −1.3 |
| ShaderTools | 1.0 | 4.1 | 5 `ShaderEffect`s without `.qsb` | precompile shaders: −1.0 resident, −4.1 APK, no runtime compile |
| Qt5Compat GraphicalEffects (+private) + Core5Compat | 1.1 | 2.0 | 86 files, 71 `layer.effect:` sites (DropShadow 37, OpacityMask 34, FastBlur 11, ColorOverlay 4, Glow 4) | file cost is small; the real cost is one offscreen texture per layered item plus blur intermediates, host-side in the Mali driver (17→45MB after login) and GPU memory outside PSS. Replace with `MultiEffect` (QuickEffects already loaded, 7 uses) and with plain items where a shadow is decorative; measure Mali host heap per screen |
| VirtualKeyboard + input-context plugin | 0.4 | 0.5 | packaged by default | unlink (native IME on mobile) |
| StateMachine + Qml plugin | 0.5 | 0.5 | 2 files | replace with plain state handling, or lazy |
| WebChannel + WebView + MobileWebView | 0.4 | 1.3 (+6.4 MobileWebView) | Browser section (4 files) | already section-scoped; load on first open |
| Labs (platform, qmlmodels, folderlistmodel) | 0.6 | 0.5 | `StatusTrayIcon`, 1–2 files each | drop on mobile |
| Lottie + VectorImage generators, Quick3DUtils, Nfc, OpenGL, QmlMeta/QmlCore/WorkerScript, image formats (tiff/jpeg/webp/ico/gif), tls backends | 2.2 | 3.5 | transitive (Lottie/VectorImage via QtQuick.VectorImage deps, Nfc via keycard flag) | exclude Lottie/VectorImage/Quick3DUtils/tiff/icns/tga/wbmp from the package; keep jpeg/webp/svg/tls |
| qmltooling (9 debug plugins), offscreen/minimal platforms, android style | 0 | 1.2 | packaged by default | exclude from release packages (APK only) |

Removable or lazy-loadable at the login screen: ~16MB resident (fault-around inflated; ~8–10 of real touched pages),
~50MB of APK. The persistent memory gain beyond that is the Qt5Compat effects → Mali host heap, which has to be
measured per screen (count of layered items × texture size) before it can be sized; the 28MB growth of Mali host
allocations after login is the upper bound.
