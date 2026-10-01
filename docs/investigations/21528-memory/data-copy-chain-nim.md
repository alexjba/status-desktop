# Data copy chain — Nim middleware (services, DTOs, items, models)

Read from the perf/21528-memory worktree, no edits. Byte figures are 64-bit estimates with `-d:useMalloc`;
Nim 2 strings and value objects copy on assignment unless `sink`/`lent`/`{.cursor.}` apply.
Symbols: M = community members, C = non-token-gated channels, Gm = members of token-gated channels,
N = communities in the service table.

## Per-member unit costs (estimates)
| form | bytes |
|---|---|
| Go JSON | ~400 |
| Nim JsonNode | ~2.5KB (14-emoji array, one JString node per emoji, dominates) |
| `ChatMember` DTO | ~0.5KB (80 inline, id 160, compressed key 64, alias 48, emojiHash 112) |
| `MemberItem` | ~0.75KB + ~160B pubkey index key |
| contacts cache entry | ~1.3KB (`ContactDetails` + `contactsStatus`) |

## Signal dispatch
- Parsed once per signal (`signals_manager.nim:45`), one typed Signal ref shared by all handlers; JsonNode lives until the emit (:65) returns.
- `messages.new` decodes every field up front whether or not anyone subscribes (`remote_signals/messages.nim:64-180`),
  incl. full communities with members (:116-118); :81-83 copies each message on add.
- Async task results go through four representations: worker JsonNode → re-serialised string (`tasks/qt.nim:22/26`) →
  UTF-16 QString at 2× (`externc.cpp:279`) → GUI-thread Nim string → parse again. Tokens are the exception (typed
  handoff, `qt.nim:28-43`).

## Communities (chats, categories, members, permissions)
1. **Go string** via `community/async_tasks.nim:11` → `qt.nim` → QString → `service.nim:926`; signal paths
   `remote_signals/community.nim:104` (community.found) and `messages.nim:116`.
2. **JsonNode** `service.nim:928`, alive through the DATA_LOADED emit at :977, so every module build happens while it
   exists; a members map is ~6× the JSON.
3. **DTO (value object)** `dto/community.nim:148-186`, parse :439-512; members :478-481; chats :486 →
   **`chat.nim:356-357` copies the full community member list into each non-gated chat**; each `ChatDto` carries ~1.5KB
   inline because it embeds `lastMessage: MessageDto` (`chat.nim:83`); tags kept as JSON string (:488-492).
   Total ≈ M×0.5KB×(1+C) + Gm×0.5KB + C×1.5KB.
4. **Service table** `service.nim:271`: insert at :948 is a deep copy (the heapprofd `eqdup`); the `communities` seq
   (:946) and response object stay alive → the load peak holds JSON, JsonNode, seq and table at once.
   Every kind of community is stored with full members: found :347 (also copied into args :348/:353), curated :1947,
   info-loaded :1730; none evicted. `handleCommunityUpdates` keeps ~5 full copies at peak (:579, :590, :530, args at
   :582-587/:614-616/:796/:801, members copy :730). Comparing members at :724 always differs for non-gated chats after
   a membership change → `updateChannelMembers` → `chat/service.nim:270-277` stores a `ChatDto` with the full member
   list per channel in the chat table (≈ C×M×0.5KB more, for the session) and emits SIGNAL_CHAT_MEMBERS_CHANGED with a
   members copy per channel; `users/controller.nim:103-109` builds MemberSeeds for each channel, even public ones.
5. **Module/model layer**: `getAllCommunities` deep-copies everything twice at load (`communities/module.nim:196`,
   `:641`; `service.nim:986` toSeq copies all N communities with members) during the load peak. SectionItem strings
   (name, description, intro, outro, image URLs, tags, color) held in the portal model (`communities/module.nim:217-252`),
   the main section model (`main/module.nim:366-438`) and a third copy for curated (:254-281). Chat section builds are
   lazy on first activation but transient-heavy: `chat_section/controller.nim:431` → `chat/service.nim:345-355` copies
   every chat in the app twice (toSeq + filterIt); `updateMissingFields` (`chat.nim:391`) copies the members into each
   channel again. **Contact seeding is permanent**: `chat_section/module.nim:387` seeds the contacts cache with every
   member; `contacts/service.nim:146-197` adds a placeholder `ContactDetails` + `contactsStatus` (~1.3KB per member),
   never evicted. Section member list: one `MemberModel` with M `MemberItem`s (~0.75KB) + ~160B per pubkey in
   `pubKeyIndex`; token-gated channels hold their own Gm items; public channels skip (`users/module.nim:229-233`).
   Every SIGNAL_COMMUNITY_MEMBERS_CHANGED rebuilds all M items with `beginResetModel` (`chat_section/module.nim:446-449`
   → `users/module.nim:247-259` → `member_model.nim:111-124`). Owners/token masters hold a second set of M items in the
   main section model (`main/module.nim:374-385`, :2272-2330). QML surface: no base64 (images are media-server URLs);
   `emojiHash` and `tags` reach QML as JSON-string roles.

## Chats and chat members
1. Go string: active chats load async (`chat/service.nim:197-224`); `messages.new` carries chats (`messages.nim:85-88`).
2. JsonNode transient (:213).
3. `ChatDto` (`chat.nim:67-104`) ~1.5KB inline (embedded `lastMessage: MessageDto`, ~1.25KB even when empty) +
   lastMessage payload ~1–2KB; group chat members ~0.25KB each, ids only (:329-338).
4. Chat table `Table[string, ChatDto]` (`chat/service.nim:126`, inserts :209/:266): slots hold the full inline value
   (2.2–4.5KB per chat with slack); `getAllChats` (:345) and filter helpers (:348-371) deep-copy every chat; callers:
   app_search (module.nim:156/:250, controller.nim:213), notifications (module.nim:75), chat_section
   (controller.nim:431/444/469/472), communities (controller.nim:300). After any member change the table also holds
   each channel's member list.
5. Items: one `ChatItem` per chat (`chat_section/module.nim:668-783`); every chat in a built section also gets a
   chat_content module (messages model, input area, users module; `chat_content/module.nim:43-63`); each
   chat_content controller registers ~15 event handlers; group chats get a users module with `MemberItem`s.

## Contacts
- Sources: async fetch (`contacts/service.nim:199-216`), `messages.new` contacts (`messages.nim:75-77`), community
  member seeds, per-pubkey synthesis on lookup miss (~3 RPCs, then cached, :435-451).
- `ContactsDto` (`contacts.nim:21-40`) ~270B inline. Cache `Table[string, ContactDetails]` (`contacts/service.nim:110`):
  duplicates name/alias as `defaultDisplayName`, thumbnail as `icon`; `contactsStatus` duplicates the pubkey (:144);
  ~1.3KB per entry incl. placeholders, never evicted. `getContactDetails` returns by value (:389-399, ~0.9KB per call),
  called per member on every member-model build and up to 3× per message (`messages/module.nim:115-129`);
  `getContactsByGroup` copies each DTO twice (:319-325).
- `UserItem` (`user_item.nim:24-50`) holds 12 strings copied from the cache. Holders: profile contacts model /
  AllKnownContacts (`profile_section/contacts/module.nim:79-99`), contact requests model, section member lists,
  token-gated channel lists, group chat lists, owner section model, and message items (two full `ContactDetails` each,
  `message_item.nim:53`, :70).

## Messages
1. Go string via signal or load (`message/service.nim:628-655`: async string round trip, parse :630, map :655);
   `MessageSignal.fromEvent` decodes all fields up front incl. full communities (`messages.nim:116-118`); :81-83 copies
   each message on add.
2. JsonNode ~3–4× JSON, transient.
3. `MessageDto` (`dto/message.nim:98-137`) ~1.25KB inline regardless of content (embeds QuotedMessage with
   DiscordMessage/BridgeMessage, DiscordMessage, BridgeMessage, TransactionParameters) + 1–2KB payload; text duplicated
   inside `parsedText` literals; link previews are QObject refs (shared); base64 `dataUri` only while composing
   (`link_preview_thumbnail.nim:68`). The same message is duplicated as `chat.lastMessage` in the signal and the chat table.
4. Service: no message cache, only cursors (:186-188); `handleMessagesUpdate` copies each message transiently (:418).
5. Item (`message_item.nim:16-78`) ~1.4KB inline (35 strings + two `ContactDetails`); text held 3× (`messageText`,
   `unparsedText`, `parsedText`, :31-34); three eager `QAbstractListModel`s per item (~0.8KB each: reactions, link
   previews, payment requests) → ~5KB per message.
6. Models: one message model per chat_content module (`messages/view.nim:27`); initial load only for the active chat
   (`chat_section/controller.nim:239`, :491); SIGNAL_NEW_MESSAGE_RECEIVED inserts into the model of any chat that has a
   content module (`messages/controller.nim:67-71`, `module.nim:292-296`) → unvisited chats grow with no cap or unload.

## Tokens
- Arrival: worker build with typed handoff (`qt.nim:28-43`, `service_main.nim:138-164`), no GUI-thread JSON parse —
  except `fetchTokenPreferences`, synchronous on the GUI thread, keeping both the raw JSON and a parsed table
  (`service_tokens_preferences.nim:14-23`); the worker path also keeps the raw string (`service_main.nim:122`).
- `TokenItem` is a ref ~0.7KB (`items/token.nim:20-38`); catalogue ~12k tokens ≈ 8MB per graph.
- Separate `TokenItem` graphs for overlapping tokens, each from its own decode, nothing shared:
  1. tokens of interest (`tokensOfInterestByKey`, `groupsOfInterestByKey`, `groupsOfInterest`, `service.nim:52-55`) — small;
  2. `allTokensByGroupKey` full catalogue (`service.nim:57`, built `token_apply_builder.nim:100-105`), refreshed at init
     and on token-list updates;
  3. `allTokenGroupsForActiveNetworks`, a second full catalogue across active chains (`service.nim:60`,
     `service_main.nim:432`), fetched on demand (All chip, search, `communities/module.nim:656`), **never released**;
  4. `allTokenLists`, one `TokenItem` per token per list (`service.nim:62`, `service_main.nim:29`), tokens in several
     lists duplicated, on demand for token-list settings, **never released**;
  5. `groupsForChain`/`groupsForChainTo` per-chain subsets (`service_main.nim:370`, :404), replaced per chain change,
     never cleared.
  → up to four catalogue-scale graphs alive at once, ~8MB each, ~25–35MB worst case.
- Smaller: `tokenDetailsTable` (descriptions, several KB per token), `tokenMarketValuesTable`, `tokenPriceTable`,
  `tokenPreferencesTable` duplicating `tokenPreferencesJson`.
- Balances: `wallet_account` `groupedAssets` holds a `BalanceItem` ref ~0.33KB per account×chain×token
  (`asset_group_item.nim:7-21`, `service_token.nim:66-74`), its strings copied not shared (5 accounts × 8 chains ×
  200 tokens ≈ 2.6MB).
- Models: five `TokenGroupsModel`s share the group refs (cheap, `all_tokens/view.nim:21-26`), `TokensModel` submodels
  lazy (`token_groups_model.nim:173-175`); main model keeps a `MarketDetailsItem` per group (5 QObjects, ~1.2KB,
  `market_details_item.nim:10-38`); `AssetsAdaptorModel` keeps two deep copies of each `AssetItem`
  (`assets_adaptor_model.nim:71-72`), the token selector model likewise.

## Collectibles (light pass)
Each row is a `CollectiblesEntry` QObject whose `setData` eagerly creates a `TraitModel` and an `OwnershipModel`
(`collectibles_entry.nim:24-57`) → three QObjects per row, ~2KB Qt+Nim overhead per collectible; data shared by ref.

## Bytes held by two or more owners, ranked
1. Community members copied into every non-gated channel (`chat.nim:357`): M×0.5KB×(1+C) per DTO copy; measured 32MB
   for the pinned Status community (M(1+C) ≈ 64k).
2. Chat table re-holds each channel's members after any member change (`community/service.nim:724-725`,
   `chat/service.nim:270-277`): + C×M×0.5KB for the session.
3. Non-joined communities (curated/found/info-loaded) keep full member lists, never evicted.
4. Load peak: JsonNode (~2.5KB/member) + parsed seq + table copy + `getAllCommunities` copies alive at once ≈ 4× item 1 + JsonNode.
5. Per-update peak in `handleCommunityUpdates`: ~5 full copies + a GUI-thread JsonNode parse of the community.
6. Member identity strings: the pubkey (~160B) stored 6–8+ times per member per activated community (ChatMember×(1+C),
   contacts key+dto, contactsStatus key+value, MemberItem, pubKeyIndex, gated lists, owner section model): ~1.3KB
   contacts + ~0.9KB member models per member.
7. Token catalogue: up to four `TokenItem` graphs ~8MB each; on-demand ones never released.
8. Messages: ~5KB per item (text 3×, two embedded `ContactDetails`, three eager submodels); `lastMessage` duplicated in
   the chat table (2–3KB per chat); unvisited chats accumulate without cap.
9. Embedded `MessageDto` (~1.25KB) in every `ChatDto` × table slack: ~3–5MB for ~1,200 chats.
10. SectionItem strings duplicated across portal, main section and curated models (intro/outro several KB each).
11. Small: `tokenPreferencesJson` + table; community tags JSON in service and view.

Unverified (`let` bound to a `lent` result without `{.cursor.}`): `activity_center/module.nim:104` (if it copies, each
activity-center item copies all channels incl. members — confirmed by value-return of the controller wrapper),
`main/module.nim:1627`, `communities/tokens/module.nim:270`; `communities/tokens/module.nim:61`/:431 store a full
community copy as a field until the next owner-token call.
