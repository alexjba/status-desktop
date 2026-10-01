# The data copy chain: status-go → Nim → models → QML

Question: status-go caches data, ships it to Nim where services cache it again, modules copy it into models, and QML
proxies/delegates copy it once more. How many copies, how big, and is it a large share of the footprint?

Layer sections with file:line evidence:
[transport](data-copy-chain-transport.md) · [status-go](data-copy-chain-go.md) · [Nim](data-copy-chain-nim.md) ·
[messages/chats](data-copy-chain-messages.md) · [QML](data-copy-chain-qml.md).

## Measured anchors (fresh profile, Redmi A5, profile build)
- UI process native heap after login ≈ 200MB, of which Nim ≈ 50MB: **32MB are `ChatDto` copies carrying the
  pinned Status community's member list once per channel**, ~13MB token catalogue items, the rest small.
- Service process live heap ≈ 40MB: token builder maps ~5MB, coingecko list transient ~15MB; contacts/chats/community
  caches negligible on an empty profile.
- Transport: one `community.found` ≈ 1.4MB JSON, ~14 copies and 2–3 full JSON parses on Android per signal; the
  community is marshalled twice inside `messages.new`.

So on an **empty** profile the chain costs ~45–50MB, almost all of it one defect (members per channel). The chain's
real cost is how it **scales**: every copy below is multiplied by members, channels, chats and tokens.

## The chain per entity (number of distinct owners of the same bytes)

| entity | status-go (resident) | wire shape | Nim service | Nim items/models | QML |
|---|---|---|---|---|---|
| community members | decoded proto members map (+ per gated chat map) in the 5-entry ttlcache; raw description ×2 | full members map for every community, re-embedded per gated chat, emojihash/alias computed per marshal; sent twice per signal | `CommunityDto.members` + a copy in **every non-gated `ChatDto`** (chat.nim:357); chat table re-holds per channel after any member change; contacts cache placeholder per member (~1.3KB, never evicted); non-joined communities stored in full | `MemberItem` per member per section (+ per gated channel, + owner section), rebuilt with reset on every change; pubkey stored 6–8× | SFPM-only (good); avatar decodes at native res |
| tokens | builder maps (13.4k `*Token`), embedded lists 3.3MB heap, token_lists blobs re-read per build | `GetAllTokens`/`GetAllTokenLists` ~3MB/3.5MB JSON, re-marshalled per call, re-pulled on every refresh | up to **four** catalogue-scale `TokenItem` graphs (~8MB each), on-demand ones never released; raw preferences JSON + table | groups models share refs (good); `MarketDetailsItem` 5 QObjects per group; `AssetsAdaptorModel` 2 copies per asset | `ManageTokensController` `TokenData` copy of all owned assets/collectibles (app lifetime); logo decodes at native res |
| chats | `allChats` incl. inactive, each with full `LastMessage`, membership history, base64 group image (≤50KB) | full `Chat` in every `messages.new` | `ChatDto` ~1.5KB inline (embedded `MessageDto`) per chat; `getAllChats` deep-copies all chats per caller | `ChatItem` + an eager chat_content module (messages model, input, users module, ~15 handlers) per chat in a built section | HomePage OPM row per chat/channel; sticky sections |
| contacts | `allContacts` with avatar payloads for every sender ever (unbounded, never sent) | full `Contact` per modified contact; `Contacts` returns all seen users | `ContactDetails` cache ~1.3KB, never evicted; by-value returns per call | `UserItem` 12 strings per holder (profile list, requests, member lists, group chats, owner model); two `ContactDetails` embedded per message item | `ContactModelEntry` per AC notification / reply delegate |
| messages | none (paged), but image payload blobs scanned per page | full message + duplicated as `chat.lastMessage` | no cache; 4 representations on the async path | ~5KB per item: text ×3, two `ContactDetails`, three eager submodels; unvisited chats grow without cap | ~60 roles copied per delegate; image messages decoded full-res per delegate, 5 chats retained |

## Is it a big waste?
- **Today (empty profile):** ~25% of the UI native heap and a few MB of Go, dominated by one bug.
- **At scale:** a 5k-member community with 30 channels costs the Nim side ≈ 60MB *per copy* of the DTO, and the
  code keeps 2 long-lived copies (community table + chat table) and reaches 4–5 copies at load/update peaks → 150–300MB
  for one community. Token graphs add 25–35MB once the on-demand views have been opened. Chats/contacts in Go grow
  with every sender ever seen. This is the mechanism behind the scaling side of #21528 (#21558), and it is CPU as
  well: every copy is a deep copy, every member change is a full model reset.

## What to do about it (architecture)
Principle: **one owner per process, views by reference.** status-go owns the data (DB + cache); it ships ids, pages
and deltas; Nim keeps one DTO per entity (ref objects, shared), items reference DTOs by key, models are windows.

1. **Members are their own entity.** status-go stops embedding members in `Community` JSON (paged
   `GetCommunityMembers`, delta signals); Nim keeps one member table per community keyed by pubkey; `ChatDto` holds no
   members (channels resolve against the community table); the chat table stops re-holding members on change;
   non-joined communities store the summary only. Removes the 32MB on a fresh profile and the 60MB-per-copy scaling.
2. **Deltas, not objects, in signals.** Community double-marshal (`CommunityChanges.Community`), full `Chat` per
   message, full `Contact` per change → ids + changed fields. Cuts the 1.4MB signals and the Java/Nim parse cost.
3. **Catalogue stays in status-go.** Nim keeps tokens of interest only; the "all tokens" views become paged/filtered
   queries (or a terminal model fed by status-go), one `TokenItem` graph with shared refs, on-demand graphs released
   when their view closes; status-go caches the marshalled list JSON per build generation and versions it.
4. **Value objects → ref objects for DTOs** (`CommunityDto`, `ChatDto`, `MessageDto`, `ContactDetails`): copies become
   pointer copies; `getAllChats`/`getCommunityById` return `lent`/refs end to end; embedded `MessageDto` in `ChatDto`
   becomes a preview struct (text, timestamp, id).
5. **Caches with eviction.** Nim contacts cache: LRU + no placeholder seeding for members; Go `allContacts`: drop
   avatar payloads from memory, LRU the never-added contacts; Go `allChats`: inactive chats lazy, images as URLs.
6. **Transport:** typed handoff (`finishTyped`) for all async results, byte buffers through JNI, one UTF-8 conversion
   and one shared-memory region per signal, prefix-scan the signal type instead of two DOM parses.
7. **QML:** `sourceSize` on every raster `StatusImage` (bounded decodes), no `cache:false` on repeated images, OPM only
   where a materialised row is needed (HomePage/AC adaptors sort on source roles instead), cap sticky sections.

Order by payoff/risk: 1 and 4 (same change set, Nim + status-go JSON shape), then 3, then 7 (cheap, visible on
every screen), then 2, 5, 6.
