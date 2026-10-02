# GPU driver host memory: 17MB at the login screen, 45MB after login

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
