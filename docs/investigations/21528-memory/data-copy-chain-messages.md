# Data copy chain — messages and chats (Nim side)

Root = status-app worktree. Evidence file:line.

## 1. Signal arrival
- `signals_manager.nim:30-65`: one `parseJson` (:45), one decode (:70-79), one shared emit (:65); no per-subscriber decode.
- `MessageSignal.fromEvent` (remote_signals/messages.nim:64-180) eagerly decodes **all** fields: full
  `CommunityDto` with members (:117-119), activity-center notifications embedding `MessageDto`s (notification.nim:84-86).
- A new message is decoded twice: `messages[]` and `chats[].lastMessage` (chat/dto/chat.nim:344-346; Go chat.go:110).
- Async fetch converts 4 times: worker `Json.decode` (backend/core.nim:35) → `Json.encode` (tasks/qt.nim:22) →
  main-thread `parseJson` (message/service.nim:630) → DTO (:655). `finishTyped` is used only by the token service.

## 2. JsonNode
Transient, ≈6–10× the JSON size (10–25KB per message); freed after the emit; no DTO keeps a JsonNode.

## 3. DTO
- `MessageDto` is a value object (message.nim:98-137): ≈1.2KB inline even when empty + ≈1–1.5KB heap.
- `parsedText` is a `seq[ParsedText]` tree. `LinkPreview` is a ref (shared), each preview a QObject.
- Images/stickers/album images are media-server URLs (Go common/message.go:39; messenger.go:3922, 3958, 3961);
  link-preview `dataUri` carries base64 only for outgoing unfurls.

## 4. Caches
- Message service holds only cursors and pinned counts (service.nim:186-188).
- `ChatService.chats` holds a full `lastMessage` `MessageDto` for every active chat (chat/service.nim:126, 209, 266;
  chat.nim:83), used only for preview text (chat_section/module.nim:760), deep-copied ~3× per signal (:155, :164, :266).

## 5. Items
- `message_item.nim:80-248` copies every field (parameters not `sink`).
- Text stored 3×: `messageText` HTML, `unparsedText`, `parsedText` (:31-34); `links` and `mentionedUsersPks` repeat it (:54, :57).
- Full `ContactDetails` copies for quoted author and deleting user (:53, :70); sender icon URL repeated per message (:28).
- 4 QObjects per item, always created (:161, :173, :195; message_model.nim:971). ≈4–7KB per item.

## 6. Models
- Every chat in a built section gets its modules and an empty model eagerly (chat_section/module.nim:200-217; chat_content/module.nim:43-64).
- Messages load on activation (chat_section/controller.nim:480-491), 20 per page; no cap or unload
  (chat_content/module.nim:410-413); inactive loaded chats keep appending (messages/controller.nim:67-71).
- Duplicate pinned model, up to 40 per chat (chat_content/view.nim:24).
- Roles that build a string on every access: message_model.nim:285, 329, 333-342, 344, 348, 350.
- Scale: ~15MB typical; ~100MB plus ~80k QObjects for a heavy user.

## Ranked duplications
1. **Community members deep-copied into every non-token-gated channel** (chat/dto/chat.nim:356-357); `getAllChats`
   copies them again, once per message, in chat_section/module.nim:146. ≈60MB per copy at 5k members × 30 channels.
   (Measured on a fresh profile: 32MB of `ChatDto` copies for the pinned Status community alone.)
2. Text stored 3×+ per item.
3. `ContactDetails` and sender-icon copies per item.
4. Newest message per chat held 3×: `ChatService.lastMessage`, the loaded item, `lastMessageText`.
5. Pinned and activity-center duplicates.
6. Per-signal transient copies (message/service.nim:399, 415, 418; chat/service.nim:155, 164, 266).
7. 4 representations on the async fetch path.
8. 4 always-created QObjects per item.
9. O(n²) album seq copies and id-set rebuilds (messages/module.nim:634-639; message_model.nim:434, 907-916).
10. Per-access string-building roles.

Confirmed: `ChatDto` (chat.nim:67) and `ChatMember` are value objects; `result.members = communityMembers` (chat.nim:357)
gives every non-token-gated channel its own copy of the member seq. The service's `getCommunityById` returns `lent`
(community/service.nim:988) but the module controller returns by value, so `getCommunityById(id).chats`
(activity_center/module.nim:104) copies every channel with its members per item built.
