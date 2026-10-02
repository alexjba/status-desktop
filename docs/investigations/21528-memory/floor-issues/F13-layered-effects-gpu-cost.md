# Qt5Compat effects: layered items drive the GPU driver's host memory (17MB at login, 45MB after)

Context: status-im/status-app#21528. heapprofd attributes 16.7MB of the UI heap at the login screen and ~45MB after
login to `libGLES_mali.so` host-side allocations; GPU memory itself is outside PSS on this device. The UI has 86 files
importing `Qt5Compat.GraphicalEffects` and 71 `layer.effect:` sites (DropShadow 37, OpacityMask 34, FastBlur 11,
ColorOverlay 4, RectangularGlow 4, LinearGradient 2), plus 7 `MultiEffect`s. Each layered item owns an offscreen
texture the size of the item (RGBA8, often with `layer.textureSize` larger than the item for DPR), and blur-based
effects add intermediate textures; the driver mirrors allocation metadata and staging on the host. The dependency's
libraries cost only 1MB of file pages; this is where its memory goes. Replacing the effect type does not remove the
layer, removing the effect does.

## What to build

1. Measure: count live layered items and their texture sizes per screen (login, wallet, chat, home, communities
   portal) with a debug hook on `QQuickItemLayer`/`QSGLayer` creation in our Qt build or via `QSG_INFO`/RHI profile
   output on desktop; record Mali host heap (heapprofd) and `Graphics` from dumpsys per screen on the Redmi and the
   footprint on the iPad.
2. Rank the sites by bytes: which shadows/masks are decorative (replace with pre-rendered assets, borders, or
   `Rectangle` tricks), which can share one layer per list delegate instead of one per child, which need
   `layer.textureSize`/`layer.smooth` reductions, which stay.
3. Land the top sites as separate PRs with the per-screen table before/after.

## Acceptance criteria

- [ ] Per-screen table: layered items, texture bytes, Mali host heap, dumpsys Graphics, before/after
- [ ] Storybook screenshots reviewed by design for every replaced effect
- [ ] Frame time on the wallet and chat lists unchanged or better

## Blocked by

Nothing; the measurement hook can land in the Qt build used by F02.
