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

## Findings (2026-10-02, Redmi A5 + macOS desktop count; raw in `profiles/floor/f13-*`, ledger `ledger/f13-effects-redmi-a5.csv`)

Method. Static: `profiles/floor/f13-static-inventory.txt` (77 `layer.enabled`, 69 `layer.effect`, 34 standalone
effect/`ShaderEffectSource` items; DropShadow 36, OpacityMask 25, FastBlur/GaussianBlur 25, MultiEffect 8, ColorOverlay 4).
Runtime count: a temporary QML timer in `main.qml` (`profiles/floor/f13-debug-helper.patch`, applied to a scratch copy of
`ui/`, packed into a scratch `resources.rcc`, never in the repo) walks the window's item tree every 10s and lists every
`QQuickShaderEffectSource` with its texture size (`textureSize` if set, else item size × DPR; ×1.34 for mipmap), the item it
samples and whether that item is effectively visible. One Qt5Compat `DropShadow` costs 4 textures of about the item's size
(layer source, GaussianBlur layer, two blur passes); a standalone `OpacityMask` costs 3 (source proxy, mask proxy, output);
`FastBlur` 1 + 7 mip levels. Desktop numbers are at DPR 1 in a 1200×680 window; multiply item-sized textures by 4 for the
Redmi (DPR 2) unless `layer.textureSize` is explicit. Device: `dumpsys gpu` "Proc <pid> total" (GPU driver memory charged to
the process, outside PSS on this Mali SoC) and `dumpsys meminfo` per screen; heapprofd could not attach (see below).

### Per screen

| screen (Redmi, r2 walk) | desktop textures (count / MB @DPR1, visible MB) | Redmi GPU MB | Redmi native heap PSS+swap MB (heap size) | EGL mtrack / Graphics MB | total PSS MB |
|---|---|---|---|---|---|
| login screen | 0 / 0 (178 items; onboarding welcome page: 4 / 0.2) | 53.1 | 96.7 (105.6) | 16.1 | 273 |
| post-login, wallet (privacy wall on device; desktop restored the portal: 100 / 31.2) | — | 88.6 | 201.1 (209.7) | 12.3 | 378 |
| chat (Messages) | 40 / 3.4, visible 0.15 | 67.1 | 205.3 (214.8) | 12.3 | 376 |
| communities portal (10 curated cards) | 100 / 31.2, visible 25.3 | **159.3** | **269.5 (280.9)** | 12.3 | **448** |
| home | 72 / 3.1, visible 2.9 | 83.1 | 236.4 (246.1) | 12.3 | 406 |
| wallet (loaded) | 68 / 2.4, visible 1.3 | 87.1 | 237.0 (247.1) | 12.3 | 407 |
| market | 27 / 2.3, visible 0.19 | 89.2 | 236.5 (246.6) | 12.3 | 407 |
| chat again | 45 / 5.4, visible 0.20 | 76.3 | 233.3 (243.1) | 12.3 | 404 |

The login screen has no layered item at all (desktop count 0; the mobile `LoginScreen` has none in the static inventory):
its 53MB of GPU memory and 16.7MB Mali host heap are swapchain buffers, atlases and decoded images, not effects. Section
views unload when another section is active (the portal grid disappears from the desktop count on Home; the device GPU
total drops 159→83MB), so the cost is per active screen plus what stays alive invisible (wallet skeletons, chat input
chrome, see sites). Opening the Communities Portal adds +76..90MB GPU and +65MB native heap (heap size 215→281MB); after
leaving it 30MB of native heap and 0 of GPU stay.

### Top sites by texture bytes (desktop DPR 1; Redmi estimate = ×4 for item-sized textures)

| # | site | effect | textures | MB desktop | Redmi est. MB | judgement |
|---|---|---|---|---|---|---|
| 1 | `ui/StatusQ/src/StatusQ/Components/StatusCommunityCard.qml:213-216` layer on the whole card, `layer.textureSize: 2×`, `layer.mipmap: true`, DropShadow samples 25; 10 cards alive in `CommunitiesGridView.qml:218` (`Flow` + `Repeater`, not virtualised) | layer+DropShadow | 10 + 30 | 16.5 + 11.8 | 16.5 + 47 (≈6.4 per card) | decorative: card shadow + rounded corners |
| 2 | same card, banner `OpacityMask` (`StatusCommunityCard.qml` banner mask, `StatusRoundedImage` logo) | OpacityMask | 42 | 2.7 | 10.6 | decorative: rounded banner, use `clip`+radius |
| 3 | `ui/StatusQ/src/StatusQ/Components/LoadingSkeletonGroup.qml:91`, `LoadingComponent.qml:62` (wallet `WalletAccountsSkeleton`, `WalletAssetListSkeleton`, `WalletCenterPanelSkeleton`) — alive and invisible on every screen after the wallet loaded (`LeftTabView.qml:254` comment says released, it is not) | OpacityMask | 15–20 | 2.0–6.9 | 8–12 | needed only while loading: unload the Loader |
| 4 | `ui/app/AppLayouts/Chat/panels/SuggestionBoxPanel.qml:38` + `StatusChatInput` DropShadow (582×156) + `StatusChatImage{Extension,Qty,Size}Validator` DropShadows + `StatusChatInputToolBar` layer — all invisible while idle | DropShadow/layer | 16 | 2.2 | 6–8 | needed when shown; bind `layer.enabled` to `visible` |
| 5 | `ui/StatusQ/src/StatusQ/Components/StatusRoundedComponent.qml:68-69` (layer + OpacityMask per instance) under each `HomePageGridSettingsItem` tile icon, 13 tiles | OpacityMask | 40 | 1.8 | 7 | decorative rounded icon tile |
| 6 | `ui/app/AppLayouts/Wallet/controls/AccountHeaderGradient.qml:65` (818×115) | OpacityMask | 3 | 1.1 | 2 | decorative gradient header |
| 7 | `ui/app/AppLayouts/Wallet/controls/BannerCard.qml:66` ×2 (buy/receive cards) | DropShadow | 8 | 0.9 | 3.6 | decorative |
| 8 | `ui/app/mainui/panels/PrimaryNavSidebar.qml:472,486` (MultiEffect mask + gradient layer, 60×339 ×2) | MultiEffect | 4 | 0.2 | 0.7 | needed (scroll fade); already MultiEffect |
| 9 | `ui/StatusQ/src/StatusQ/Components/StatusRoundedComponent.qml:68-69` via `StatusRoundedImage`/`StatusSmartIdenticon` (3 textures per avatar: 24–72 px, ~10–20 per list screen) | OpacityMask | 20–35 | 0.1–1.0 | 0.4–4 | decorative circle: use `Rectangle{radius}`+`clip` or one shared mask |
| 10 | `ui/app/AppLayouts/Onboarding/controls/OnboardingFrame.qml:24`, `OnboardingButtonFrame.qml:22`, `WelcomePage.qml:110` (onboarding only) | DropShadow/OpacityMask | 14 | 2.9 | 11 | decorative, transient |
| 11 | `ui/app/AppLayouts/Communities/views/*Community*View.qml` + `panels/*CenterPanel.qml` FastBlur over the whole center column (8 sites, join/banned/offline states) | FastBlur | 8 per site | not hit in walk | ~(screen×4)×1.33 ≈ 15 when shown | decorative dim; replace with a semi-transparent Rectangle |
| 12 | `ui/app/AppLayouts/Onboarding/pages/BackupSeedphraseReveal.qml:121` GaussianBlur samples 64 over the seed grid | GaussianBlur | 3 | not hit | ~3 when shown | needed (hides seed); fine as transient |

Not texture-related but found on the way: `layer.samples` (MSAA) is used only by `CountdownProgressIndicator.qml:40`
(samples 4); the `samples: 15–64` elsewhere are Gaussian kernel sizes (GPU time, not memory).

### Ranked changes (estimates, Redmi A5, GPU memory unless stated; the Mali host-side mirror adds a fraction of each)

1. Communities portal cards: virtualise the grid (`GridView`/`ListView` instead of `Flow`+`Repeater`, 2–3 cards on a
   phone instead of 10) and replace the per-card Qt5Compat DropShadow by a `MultiEffect` shadow or a 9-patch shadow asset;
   drop `layer.textureSize: 2×` and `layer.mipmap` (on DPR 2 the explicit size is the default, mipmap is +34%; on DPR 1
   desktops it is 4×). Est. **−60..70MB GPU and −20..40MB native heap while the portal is open** (159→~90MB GPU).
2. Release the wallet skeletons after load (`LeftTabView` and `WalletCenterPanel` Loaders → `active: loading`). Est. −8..12MB
   GPU persistent on every post-login screen.
3. Gate the chat input chrome layers on visibility (`SuggestionBoxPanel`, validators, toolbar, `StatusChatInput` shadow).
   Est. −6..8MB GPU persistent while a chat is open.
4. Replace rounded-corner `OpacityMask`s (home tiles, avatars, community banner, `AccountHeaderGradient`) by `clip`+`radius`
   or a single shared `MultiEffect` mask; most are 3 textures for a corner radius. Est. −10..15MB GPU on home/portal/chat.
5. Qt5Compat `DropShadow` → `MultiEffect` everywhere (36 sites): 4 item-sized textures → 1 item-sized + a quarter-size
   blur chain. Est. −50% of each site's shadow bytes; −3..5MB on wallet/chat screens, bulk of the win is in 1.
6. FastBlur dim panels (8 community-state sites) → translucent `Rectangle`: −15MB GPU when a join/banned/offline view is shown.

What could not be measured: Mali host-side heap per screen. The installed `app.status.mobile` is the Sep 2 release APK
without `<profileable>`, so heapprofd produced an empty trace (`profiles/floor/f13-heapprofd-redmi-a5-EMPTY.txt`); the
profiling build is only installed as `app.status.mobile.debug` (not started). `adb install -r` of
`mobile/bin/android/qt6/Status.apk` (same debug signing key, keeps the profile data) would enable the heapprofd column;
the native-heap delta at the portal (+65MB heap size) is the upper bound for the Mali host mirror of its +90MB of GPU
textures. Desktop count caveat: macOS DPR 1 and a 1200×680 window; mobile layouts (`StatusSectionLayoutPortrait`) show the
same components full-width, so per-site counts transfer, sizes do not.
