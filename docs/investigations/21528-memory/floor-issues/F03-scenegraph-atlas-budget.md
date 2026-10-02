# GPU driver host memory: 17MB at the login screen, 45MB after login

**Expected gain:** memory, measured. Atlas 1024² instead of the per-surface 2048×4096: iPad −28MB at the login screen (197→168, repeatable ±2), −25..−40MB at the wallet settle, identical screenshots. Android uses the same formula (32MB inside the Mali driver's host allocations) and is expected in the same range; to confirm on the Redmi. APK: none. Change is one `putEnv` line.

Context: status-im/status-app#21528. heapprofd attributes 16.7MB of the UI heap at the login screen to
`libGLES_mali.so` host-side allocations (`eglCreateContext` 3.4MB, texture staging under `glTexSubImage2D`,
per-frame allocations under `QRhi::endFrame`), 45MB after login, plus 16MB of EGL window buffers (`EGL mtrack`).
The scene graph runs with Qt defaults: texture atlas 2048×2048 RGBA (16MB when allocated), distance-field glyph
cache, `QSG_RENDER_LOOP=threaded` only. No `QSG_ATLAS_*` or glyph-cache settings are applied.

## What to build

1. Measure what the Mali host allocations correspond to: atlas size, glyph caches, swapchain depth, shader program
   cache. Instrument with `QSG_INFO=1`, `QSG_RHI_PROFILE=1` and the heapprofd callstacks under `libGLES_mali.so`.
2. Try the cheap knobs on the Redmi and the iPad: `QSG_ATLAS_WIDTH/HEIGHT=1024`, smaller glyph cache, texture
   compression off/on, and record PSS, EGL mtrack and frame time (no visual regression on the wallet and chat views).
3. Apply the best setting at startup on mobile (`putEnv` next to `QSG_RENDER_LOOP`), per device class if needed.

## Acceptance criteria

- [ ] Table: setting → UI PSS at login screen / settle, EGL mtrack, 95th percentile frame time on the wallet list
- [ ] Chosen defaults applied for Android and iOS mobile builds, documented in `floor.md`
- [ ] No visible texture artefacts (atlas too small) in the storybook screenshot suite

## Blocked by

Nothing.

## Findings (iPad Air 4, A14, iOS 26.5.2, profiling build 21, 2026-10-02)

Method: `Status-b21.app` relaunched per variant through `devicectl` with the knob injected into the environment
(`DEVICECTL_CHILD_*`), console captured. Login screen read 66 s after launch (the in-process VM summary is written
at 60 s); WebDriverAgent then types the test profile password and taps Log In (~70 s); the wallet settle point is read
at 246 s after launch (VM summary written at 240 s, ~170 s after Log In). Login-screen numbers repeat within ±2 MB.
The settle number is noisy (±13 MB: compressor and Go heap state) and the 15 s series jumps by 15–35 MB at 195–210 s
when the minute-ly Go heap profile is written, so the table also gives the pre-jump plateau (samples 105–195 s after
launch, i.e. 35–125 s after Log In), which is the better settle comparison.
`QSG_INFO` lines are `qCDebug`, which the app's chronicles handler drops at INFO: `STATUS_RUNTIME_LOG_LEVEL=DEBUG`
was needed to see them (baseline measured with and without it: 196 vs 197 MB at login, same).

| variant | login phys MB | settle plateau MB | settle @246 s MB | IOSurface | IOAccelerator resident login / settle | malloc dirty login / settle | Go dirty login / settle |
|---|---|---|---|---|---|---|---|
| baseline (Qt defaults) | 197 | 416–420 | 439 | 45.0 | 3.3 / 5.0 | 80.5 / 196 | 30 / 116 |
| baseline, DEBUG log | 196 | 407–414 | 428 | 45.0 | 3.3 / 4.8 | 80.7 / 210 | 31 / 120 |
| `QSG_ATLAS_WIDTH=1024 QSG_ATLAS_HEIGHT=1024` | **169** | **374–382** | 414 | 45.0 | 3.4 / 4.8 | 80.6 / 184 | 30 / 102 |
| same, repeat | **168** | **384–389** | 424 | 45.0 | 3.4 / 28.8 | 80.5 / 204 | 31 / 114 |
| `QSG_ATLAS_WIDTH=512 QSG_ATLAS_HEIGHT=512` | 166 | 400–413 | 420 | 45.0 | 3.4 / 5.1 | 80.6 / 191 | 30 / 116 |
| `QML_DISABLE_DISTANCEFIELD=1` (native glyph cache) | 197 | 423–424 | 443 | 45.0 | 3.3 / 27.8 | 80.7 / 188 | 31 / 116 |
| `QSG_TRANSIENT_IMAGES=1` | 193 | 426–429 | 435 | 45.0 | 3.4 / 4.8 | 76.5 / 177 | 30 / 105 |
| atlas 1024² + `QSG_TRANSIENT_IMAGES=1` | **166** | **376–378** | **391** | 45.0 | 3.4 / 7.2 | 76.5 / 199 | 31 / 121 |

Visual check: WDA screenshots of the login screen and of the wallet (assets list, token logos, popup text) are
identical across all variants; no missing glyphs or texture artefacts at 1024² or 512².

### What Qt prints and what the budget is

- `rhi texture atlas dimensions: 2048x4096` — for the 1640×2360 portrait surface Qt takes the next power of two per axis
  (`qsgrhiatlastexture.cpp`: `max(512, nextPowerOfTwo(surface-1))`), so the atlas is **2048×4096 RGBA8 = 32 MB**, created
  on the first atlas upload (already at the login screen). `QSG_ATLAS_SIZE_LIMIT` defaults to `max(w,h)/2` = 2048, so
  every image up to 2048 px goes into it. The texture is `MTLStorageModePrivate`: it is not mapped into the task and
  appears in no row of the VM summary (IOAccelerator stays at 3–5 MB), but it is charged to `phys_footprint` through the
  IOKit ledger — which is exactly the 28 MB that disappears at the login screen with 1024² (197 → 169; a 1024² atlas is
  4 MB). The same formula runs on Android (Mali): a 1080×2400 phone also gets 2048×4096.
- `Metal device: Apple A14 GPU`, `TextureSizeMax` 16384 (MTLGPUFamilyApple3+), `MSAA sample count for the swapchain is
  1. Alpha channel requested = no`, `threaded render loop`, `got CAMetalLayer, pixel size 1640x2360 (scale 2.00)`.
- IOSurface 47.15 MB = 3 CAMetalLayer drawables × 15.5 MB (2360×1640×4) + ~0.7 MB. Qt does not set
  `maximumDrawableCount` (iOS default 3); 2 drawables would need a change in the QIOS platform plugin, there is no env
  knob. Not measured.
- Swapchain depth-stencil: on iOS Qt creates it `MTLStorageModeMemoryless` (`qrhimetal.mm`) → 0 bytes, so
  `QSG_NO_DEPTH_BUFFER` / `QSG_NO_STENCIL_BUFFER` cannot save memory here (not run; on Android/Mali it is a real buffer).
- Glyph cache: distance-field, `RED_OR_ALPHA8` textures (1 byte/px) grown on demand (area allocator up to 16384² × 3),
  private storage. Switching to the native glyph cache (`QML_DISABLE_DISTANCEFIELD=1`) is **worse**: +23 MB of
  host-visible GPU regions (IOAccelerator resident 4.8 → 27.8) and no footprint gain. Qt 6.11 has no distance-field
  cache size knob; `QT_DISTANCEFIELD_DEFAULT_BASEFONTSIZE/SCALE/RADIUS` change glyph tile size (quality), and
  `QSG_PREFER_FULLSIZE_GLYPHCACHE_TEXTURES` allocates full 16384² textures up front — do not set.
- `QSG_TRANSIENT_IMAGES=1` drops the CPU-side `QImage` in `QQuickDefaultTextureFactory` after the texture is created:
  −4 MB malloc at the login screen, settle malloc dirty 177 vs 184–210 (inside the settle noise). Risk: when the scene
  graph is invalidated (iOS releases SG resources on background) images are re-decoded from source. Candidate for
  #22628 after a background/foreground check.
- `QSG_ENABLE_COMPRESSED_ATLAS` only applies to pre-compressed (ETC/ASTC) textures — our assets are PNG/SVG, no effect.
- `QSG_RENDER_LOOP`: the app does `putEnv("QSG_RENDER_LOOP", "threaded")` unconditionally
  (`src/nim_status_client.nim:320`), so the basic loop cannot be selected from the environment; not measured.
- The transient 24 MB IOAccelerator region seen at settle in two runs (native glyph run, atlas-1024 repeat) is a
  host-visible staging allocation in flight at the time of the summary, not a steady-state cost.

### Deltas and recommendation

- Atlas 1024²: **−28 MB at the login screen** (197 → 168/169), **−25 to −40 MB at the settle plateau** (374–389 vs
  407–420); 512² saves 3 MB more at login but nothing more at settle (atlas pages are created on demand and the size
  limit drops to 256 px, so more standalone textures). Combined with transient images: −31 login, −35/−40 settle.
- Recommendation: `putEnv("QSG_ATLAS_WIDTH", "1024"); putEnv("QSG_ATLAS_HEIGHT", "1024")` next to `QSG_RENDER_LOOP`
  on mobile (both platforms — the default formula is the same on Android and the 32 MB sits in the Mali driver there,
  Android numbers still to be taken). Keep the distance-field glyph cache. Depth/stencil knobs are free on iOS. Try
  `QSG_TRANSIENT_IMAGES=1` only after the background-resume check.
- Not covered: Android (other agent), p95 frame time on the wallet list, render-loop basic vs threaded (needs a code
  change), drawable count 2 (needs a platform-plugin change).

### Raw data and commands

- `profiles/floor/f03-<variant>-{login,settle}-vm.txt` (in-process VM summaries), `profiles/floor/f03-<variant>-qsg.txt`
  (Qt scene-graph/RHI log lines + 15 s footprint series), `ledger/f03-atlas-ipad.csv`.
- Scripts: `mobile/scripts/profiling/f03-run.sh <label> [VAR=VAL …]` (launch → login-screen snapshot → WDA login →
  settle snapshot), `f03-launch.sh`, `f03-pull.sh`, `f03-collect.sh`. WDA must be running (`wda-run.sh`).
- Launch command (per variant, from `f03-launch.sh`):

```
env DEVICECTL_CHILD_QSG_INFO=1 \
    "DEVICECTL_CHILD_QT_LOGGING_RULES=qt.scenegraph.general=true;qt.rhi.general=true" \
    DEVICECTL_CHILD_STATUS_RUNTIME_LOG_LEVEL=DEBUG \
    DEVICECTL_CHILD_QSG_ATLAS_WIDTH=1024 DEVICECTL_CHILD_QSG_ATLAS_HEIGHT=1024 \
  xcrun devicectl device process launch --console --terminate-existing \
    --device 00008101-000474143446001E app.status.mobile > f03-atlas1024.log
# login screen at +66 s, then:
python3 wda.py tapxy 410 649; python3 wda.py text TestPassword2026; python3 wda.py tap "Log In"
# VM summary pull (login at +66 s, settle at +246 s):
xcrun devicectl device copy from --device 00008101-000474143446001E --domain-type appDataContainer \
  --domain-identifier app.status.mobile --source "Library/Application Support/Status/logs/vm-summary-latest.txt" \
  --destination f03/<variant>-<point>-vm.txt
```
