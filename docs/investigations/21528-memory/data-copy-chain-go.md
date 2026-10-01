# Data copy chain — status-go side (caches, API shapes, serialisation)

status-go at perf/21528-madvise-dontneed @37ffb6460. Evidence file:line; sizes estimated from struct shapes.

## Copy chains (every entity)
- RPC on desktop (`CallPrivateRPC`), ~7 copies: geth `json.Marshal(result)` + copy out (go-ethereum rpc/json.go:117)
  → envelope marshal compacting the RawMessage (pkg/backend/node/rpc/rpc.go:50) → `string(data)` (:55) → `C.CString`
  (tools/generate-cbindings/main.go:100) → Nim `$cstring` (src/status_go.nim:80-82) → `Json.decode` tree
  (src/backend/core.nim:35-36). With request logging on, `callog.LogCall` marshals + unmarshals again.
- Signals: `json.Marshal(Envelope)` → `string` → `C.CString` (internal/signal/signals.go:51,63). Nested Marshalers
  (`Community.MarshalJSON` → `MessengerResponse.MarshalJSON` → envelope) each add an encoder buffer, a copy out and a
  compact into the parent: community bytes are copied ~7× before C, ~3–4× payload alive at peak. `LogSignal` adds
  marshal+unmarshal+zap when request logging is on.
- Android extra hops: see data-copy-chain-transport.md (RPC ~12 copies, ~4 of them UTF-16; signals ~10 copies + 2–3 full
  JSON DOM parses in the service process; SharedMemory created per listener).
- Correctness flag (unverified): `NewStringUTF`/`GetStringUTFChars` use modified UTF-8; emoji (4-byte) in every
  emojiHash may come back as CESU-8 surrogates to Nim.

## 1. Communities
| owner | file:line | per item | lifetime |
|---|---|---|---|
| `Manager.cache` ttlcache[string, ReadonlyCommunity] | communities/manager.go:121,324 | full `*Community` | cap 5, TTL 1m |
| `Community.config` | community.go:71-91 | raw signed wrapped description bytes + decoded `CommunityDescription` proto (Members map + per-chat Members maps + TokenPermissions + Identity.Images payloads + PrivateData) + `EventsData.EventsBaseCommunityDescription` (another raw copy, persistence.go:1429-1445) | per decoded object |
| `recentlyPublishedOrgs` | messenger_communities.go:330,400 | `CreateDeepCopy` (proto.Clone) per controlled community | messenger lifetime, never evicted (control node only) |
Each decoded Community holds the description ~3× (raw, events-base raw, decoded). 5k members ≈ 1.5–2MB decoded; each
token-gated chat's member map decoded separately at similar size.

Read APIs (pkg/services/ext/api.go): `Communities` (:217) → `manager.All()` = **all stored communities incl.
non-joined/curated/spectated** (messenger_communities.go:976; persistence.go:259), re-read and `proto.Unmarshal`ed on
every call (persistence_mapping.go:91-131), no cache; desktop calls it (src/backend/communities.nim:29).
`CuratedCommunities` (:244) returns `Descriptions map[string]*Community` → non-joined communities in full (manager.go:686-721).

Shape (`Community.MarshalJSON`, community.go:297-454): full Members map for **every** community, joined or not (:409);
each token-gated chat re-embeds the whole channel member map (:399-401); `enrichMembersMap` (:48-63) calls
`GetPublicKeyData` per member per marshal, uncached (compressedKey + 14-emoji emojiHash + alias + colorId,
accounts-management/common/publickey.go:15-45) ≈ 400B JSON per member → 5k members × (1+5 gated chats) ≈ 12MB per marshal.

Signals: every received description goes through `Response.AddCommunity` **and** `CommunityChanges` whose `.Community`
is the same pointer with `json:"community"` (messenger_communities.go:3217-3218; community_changes.go:21,207) → the
`messages.new` signal marshals the full community **twice**; same for category changes (:1820-1886).
`SendCuratedCommunitiesUpdate` (messenger_curated_communities.go:82) and `SendCommunityInfoFound`
(pkg/services/ext/signal.go:52) send full objects.

## 2. Chats
| owner | file:line | per item | lifetime |
|---|---|---|---|
| `Messenger.allChats` (sync.Map) | messenger.go:121; messenger_maps.go:22; messenger_filter_init.go:90,130-142 | `Chat` ~400B + `LastMessage *common.Message` (json-restored, persistence.go:384-390, ~2–5KB) + Members + MembershipUpdates history (gob, persistence.go:350-362) + `Base64Image` data URI of the group image (persistence.go:377-381, up to ~50KB) | all chats from DB **incl. inactive**, forever |
APIs: `Chats`/`ActiveChats` return full `Chat` (api.go:112,124); desktop uses activeChats (src/backend/chat.nim:37);
`ChatsPreview` is lighter (messenger_chats.go:56). Signals: every incoming message `AddChat`s its chat
(messenger.go:3574-3587) → each `messages.new` carries the full Chat incl. LastMessage (duplicating the message in
Messages), MembershipUpdates and the base64 image.

## 3. Contacts
| owner | file:line | per item | lifetime |
|---|---|---|---|
| `Messenger.allContacts` (sync.Map) | messenger.go:124; messenger_filter_init.go (persistence.go:557-712) | `Contact` (contacts/contact.go:88-137) + `Images` map with raw Payload bytes (thumbnail ≤5.6KB + large ≤37.5KB, images/meta.go:35-43) | forever, never evicted |
Any unknown sender of any message gets `BuildContact` and is stored (messenger.go:3437-3464, :3110, :3564-3567);
`HandleChatIdentity` attaches image payloads (messenger_handler.go:2821-2825) → **unbounded**, ~20–40KB per member
with avatars. The payload is never sent (`IdentityImage.MarshalJSON` drops it, images/identity.go:40-62; clients use the
media server). `Contacts` API returns all (api.go:170 → messenger_contacts.go:769); desktop calls it
(src/backend/contacts.nim:9); `Contact.MarshalJSON` is reflection + `GetPublicKeyData` per contact per call
(contact.go:478-517). Signals: `messages.new` includes the full Contact per modified contact (messenger.go:3557-3561).

## 4. Messages
No in-memory cache; cursor-paged (api.go:533 → messenger.go:3783 → message_persistence.go:784). Each page scans
`m1.image_payload` into `ImageMessage.Payload` (message_persistence.go:175,324) — full image blobs allocated per page
although JSON sends a media-server URL. Quoted album images `json.Marshal`ed per message (messenger.go:3895).

## 5. Tokens / balances / prices
| owner | file:line | per item | lifetime |
|---|---|---|---|
| go-wallet-sdk `manager.builder.tokens` + `tokenLists` | builder.go:31-35; manager.go:36-66 | `types.Token` ~300B; tokenLists unfiltered by chain (manager.go:57-59,471) | rebuilt on refresh |
| embedded default lists `DownloadedTokenList.JsonData` | token/local-token-lists/default-lists/*.go (`init()`: `JsonData = []byte{…}`) | 3.30MB raw JSON, 13,378 tokens / 8 lists | process lifetime; a `[]byte` literal in `init` is heap-copied → ~3.3MB heap kept after remote lists load |
| `token_lists` blobs (contentStore) | token/tokenlists_adapter.go:13-31 | raw list JSON | `GetAll()` on every `buildState` (manager.go:596) → transient multi-MB |
| `multistandardbalance.StorageMemory` | storage_memory.go:12-18 | per (account, chain): map[contract]*big.Int (+721/1155) ~100B/entry | forever |
| `market.Manager` priceCache/marketCache | market/market.go:52-62 | per (token, currency) ~100B | no TTL, bounded by tokens of interest |
APIs: `GetAllTokenLists` (wallet/api.go:150; desktop token/service_tokens.nim:48, async_tasks.nim:97) returns every
list with every token (~13.4k, ~3.5MB JSON), wrapping each token in a new `tokentypes.Token` (token/token.go:710-739),
no cached JSON; `TokenListsUpdated` is trigger-only (token.go:296) so each refresh re-pulls the 3.5MB.
`FetchOrGetCachedWalletBalances` (api.go:119 → reader.go:363-407) reads the whole `token_balances` table for all
addresses, merges with memory storage, then filters. Wallet events double-encode JSON (`json.Marshal(payload)` →
string → `Event.Message` → envelope escapes it again: leaderboard/service.go:255-327, activity/service.go:366,
collectibles/manager.go:1093). Leaderboard pushes full data every 10s and prices every 1s while subscribed.

## 6. Installations / settings
`allInstallations` small. `GetSettings` is called per ChatIdentity message (messenger_handler.go:2761) — CPU/DB, not retained.

## Ranked: largest avoidable retained bytes
1. Contact identity image payloads for every sender ever (unbounded). Remedy: don't keep `Payload` in memory; keep
   added/recent contacts in the map, LRU the rest.
2. `Chat.Base64Image` + full `LastMessage` + `MembershipUpdates` for all chats incl. inactive. Remedy: image as
   URL/id, `LastMessage` as id/preview, lazy membership history.
3. Embedded token lists ~3.3MB heap + ~13.4k Tokens across unfiltered lists. Remedy: `go:embed`/rodata instead of a
   `[]byte` literal, drop the fallback once remote lists are stored, filter to enabled chains.
4. Community description held 3× (raw, events raw, decoded). Remedy: keep decoded only / decode lazily.
5. `recentlyPublishedOrgs` deep copies, never evicted (control node). Remedy: keep clock + key-action state only.

## Ranked: largest avoidable per-request / per-signal copies
1. Community double-marshal in every description signal (`Communities[]` + `CommunityChanges[].Community`). Remedy:
   `json:"-"` or id only; send member deltas, not the full community.
2. Full members map for every community, re-embedded per gated chat, with uncached emojihash/alias/compressedKey per
   member. Remedy: drop members from Community JSON (page via a members API), omit for non-joined communities,
   memoize `PublicKeyData` by pubkey.
3. `Communities` RPC returns all stored communities, re-decoded per call. Remedy: joined/spectated only or a light
   summary list + per-id detail.
4. Chat object with LastMessage + base64 image + membership history on every incoming message. Remedy: chat delta
   (unread counts, lastMessageId, timestamp).
5. `GetAllTokenLists` ~3.5MB re-pulled on each `TokenListsUpdated`. Remedy: cache marshalled JSON per build
   generation, filter by chain, version/etag so the client skips unchanged lists.
6. `Contacts` RPC returns every seen user with reflection + emojihash per contact. Remedy: added contacts only, page
   the rest, memoize `PublicKeyData`.
7. Android service double JSON DOM parse per signal + per-listener SharedMemory. Remedy: prefix-scan `type`, check
   `uiVisible` before parsing, one UTF-8 conversion, one shm region.
8. Go marshal nesting + `string()`+`CString`. Remedy: marshal straight into a C buffer (`C.CBytes` once), RawMessage
   passthrough for nested Marshalers.
9. Wallet event double-encoded JSON and 1s leaderboard pushes. Remedy: `json.RawMessage` Message; throttle/delta.
10. Message page scans `image_payload`. Remedy: drop the column from the list query.
