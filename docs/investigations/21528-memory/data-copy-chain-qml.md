# Data copy chain — QML / StatusQ model layer

perf-21528-memory @ c908044f9d; QtModelsToolkit 84d2433e. Estimates, not measured.

## Ranked duplication sites
1. **Images decoded at native resolution.** `StatusImage.qml:48` sets `sourceSize` only for `.svg`; every avatar,
   token logo, community logo and collectible thumb (`StatusSmartIdenticon` → `StatusRoundedImage` → `StatusImage`)
   decodes at source size. Token `logoUri` is typically 256–1024px → 0.25–4MB RGBA per logo vs ~6KB at 40px; remote NFT
   art in `ManageTokensDelegate.qml:88-96` can be 2k px (~16MB). Bound: distinct URLs on screen + Qt's unreferenced
   pixmap cache; each decode keeps one CPU QImage + one GPU texture.
2. **Chat image messages: private full-res decode per delegate.** `StatusImageMessage.qml:54-68` AnimatedImage with
   `cache:false`, no `sourceSize`, layer+OpacityMask (extra FBO); `StatusChatImageLoader.qml:56-63` same + `mipmap`
   (+33%). Retained for 5 chats (`loader_deactivator.nim:4` MAX_CHATS_IN_MEMORY=5); `ChatColumnView.qml:456-478` keeps
   hidden chat views alive with their delegates; cacheBuffer 2 screens (`ChatMessagesView.qml:254`). ~8MB per 1080p image.
3. **HomePageAdaptor materialises an ObjectProxyModel row for every row.** `filteredCombinedModel`
   (`HomePageAdaptor.qml:431-455`) filters/sorts on exposed roles; `pinnedModel` (155-167) too; sources: communities,
   settings, chats, all community channels, wallets, dapps (178-331); overlay OPM on top (378-388). Per row: QObject +
   QQmlContext + QQmlPropertyMap copying expected roles + a second proxy; `load()`/`clear()` touch every row. ~2–5KB/row
   × hundreds–thousands → 1–10MB. Bounded by model size; lives while Home is active (not sticky, `AppMain.qml:2263-2267`).
4. **ActivityCenterAdaptor materialises every non-dismissed notification** (`ActivityCenterAdaptor.qml:201-208` outer
   SFPM sorting on an exposed role over the OPM 260-318): per row a QtObject, an `ActivityNotification` copying ~25
   roles (317-342), an Instantiator, a per-type adaptor (413-430) and a `ContactModelEntry` (QQmlPropertyMap + a
   `ContactDetails` object, `NotificationSenderResolver.qml:91`) + parsed community/chat JSON (514-517).
   `acPanelLoader` has no `active:` (`AppMain.qml:2045-2058`) → app lifetime.
5. **ManageTokensController copies all owned assets and all owned collectibles into `TokenData`** (13 QStrings + 5
   QVariants, `wallet/tokendata.h:10-21`; built in `addItem`, `managetokenscontroller.cpp:588-645`; `parseSourceModel`
   417-451 at `componentComplete` 378-381). Instances `WalletAssetsStore.qml:45`, `CollectiblesStore.qml:54`, both in
   the `WalletStores.RootStore` singleton → app lifetime; ~0.5–1.5KB/row; `addItem` also copies `roleNames()` per row.
6. **Communities portal: non-virtualised `Flow`+`Repeater`** (`CommunitiesGridView.qml:218-280`) of
   `StatusCommunityCard`, each with a banner Image `cache:false` (`StatusCommunityCard.qml:246-255`) + OpacityMask
   layers; banners up to 800px (~1MB decoded each); every curated card exists at once. Not sticky.
7. **Sticky per-community sections pin proxy chains.** `CommunityChatLoader` sticky via Binding RestoreNone
   (`AppMain.qml:2475-2498`), no cap, each with its own `ChatStores.RootStore` (`CommunityChatLoader.qml:224-234`):
   `assetsModel` SFPM + FastExpressionRole over `communitiesModuleInst.tokenList` (built from all token groups for
   active networks, `communities/module.nim:655`); `walletCollectiblesModel` OPM over `allCollectiblesModel` (144-157)
   → GroupingModel → SFPM → RolesRenaming → Concat (184-193), rows never freed until reset (materialisation trigger
   unverified); `CommunitySettingsLoader` sticky after first open (`ChatLayout.qml:485-487`). Personal `ChatLoader`
   (2287-2297) and Browser (2364-2372) also sticky.
8. **Emoji DB held twice**: `emojiList.js` (1.6MB, 3773 entries) as JS objects in the `Emoji` singleton
   (`Emoji.qml:8,19`) and again in `StatusEmojiModel`'s `QJsonArray` (`statusemojimodel.h:56`; `Emoji.qml:21-22`),
   ~4–8MB, app lifetime.
9. **Message delegates copy ~60 roles each** (`ChatMessagesView.qml:335-430`: messageText + unparsedText + quoted
   texts + album `split()` arrays); reply/pin delegates add a `ContactModelEntry` (`MessageView.qml:201-215`);
   `StatusMessage.qml:401-405` an attachments ListModel per delegate. Bounded by visible + cacheBuffer × 5 chats.
10. **SharedAddressesAccountSelector** (popup): `modelToArray(filteredWalletCollectibles)` copies all roles recursing
    into submodels (137), re-copied into a ListModel (130-164); each account delegate creates an OPM over
    `walletAssetsModel` with an SFPM per asset (181-197); metadata `modelToArray` on every revision (48-50).
11. Transient churn only: `SendModalHandler.qml:313-321` `ModelUtils.get` per tokenGroups row; `GroupsModel.qml`
    FastExpressionFilter scanning each row's tokens submodel; vendor ExpressionRole/Filter building a full-row
    QVariantMap per evaluation (`expressionrole.cpp:68-81`, ~10 popup sites).

## Proxy classes: index-only vs materialising
- Index-only: SFPM (mapping vectors + `m_proxyRoleMap`, `qqmlsortfilterproxymodel.h:135-136`; FastExpression* no
  per-row cache), LeftJoinModel (QCache ≤1000 persistent indexes, miss = O(N) `match()`), ConcatModel,
  RolesRenamingModel, GroupingModel (3 ints/row + 1 RangeModel per group), MovableModel.
- Materialising: ObjectProxyModel (lazy per row, but any sort/filter on an exposed role creates all rows; row = QObject
  + QQmlContext + QQmlPropertyMap; never freed until removal/reset; `dataChanged` rewrites the map,
  `objectproxymodel.cpp:251-297`, 135-148); ModelEntry (one-row property map; `cacheOnRemoval` deep-copies submodels
  into SnapshotModel, `modelentry.cpp:423-449`; 52 production sites, mostly popups; `ContactModelEntry` uses
  `cacheOnRemoval:true`); WritableProxyModel (per edited row); `ModelUtils.modelToArray` (JS copy recursing into
  submodels, ~35 sites, mostly one-shot popups); ManageTokensModel (`QList<TokenData>`).
- Dead: `CollectiblesSelectionAdaptor` (replaced by Nim, `SendModalHandler.qml:613`). Token pickers use the Nim terminal
  `TokenSelectorModel`; member lists are SFPM-only over Nim models.

## Image facts
Community/profile images are media-server URLs (status-go `community.go:442`, `contacts.nim:76`, `chat.nim:183-196`);
sizes thumb 80 / large 240 / banner 800 (`images/meta.go:18-21`). Same URL without `sourceSize` and `cache:true`
dedupes to one decode across delegate + picker + header; SVG logos decode once per distinct item size; `cache:false`
sites decode per Image (community card banner, chat images, `CollectiblesHeader.qml:34`, `ActivityCenterPanel.qml:362,427`).
No QPixmapCache/QQuickImageProvider configuration. NAM disk cache 512MB, also stores localhost media-server responses.

## Lifetimes
App lifetime: `WalletStores.RootStore` singleton, AppMain-level contact adaptors, ActivityCenter adaptor,
PopupsLoader/HandlersManagerLoader once activated (never reset, `PopupsLoader.qml:59,122`;
`HandlersManagerLoader.qml:47,106`). Unloaded on navigate: Home, Portal, Wallet, Profile, Market.

Not verified: real row counts, per-row bytes, whether `walletCollectiblesModel` OPM rows materialise in community sections.
