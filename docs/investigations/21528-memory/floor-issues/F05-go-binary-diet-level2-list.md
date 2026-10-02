# Go binary diet, level 2: sized feature-removal list for product sign-off

Context: status-im/status-app#21528. After the dead-code pass (level 1) the remaining size of the mobile Go library
is features: libp2p transports and services the mobile client may not need (WebRTC via `pion/*` ~0.9MB text, QUIC
~0.7MB incl. `gojay` qlog, DNS/mDNS discovery), go-waku store/filter/lightpush code paths, wallet providers and
contract bindings (`internal/contracts/*` ~0.5MB), community token deployer bytecode in rodata, stickers, ENS name
wrapper, pairing, connector (dapp bridge), push-notification client, RSS/news, shared URLs, and the two protobuf
runtimes. Each is a product decision, not an engineering one.

## What to build

A table, one row per removable feature or transport: package set, MB of text, estimated resident MB on the
service process (text + pclntab + relocs ≈ 2× text at the login screen), what the mobile user loses, and the
build-level mechanism (build tag, option, fork of a default list, go-waku/libp2p configuration that actually drops the
code). Verified by building the library without the row and measuring, not estimated from `nm` alone, for the three
largest rows. Delivered as a section in `docs/investigations/21528-memory/floor.md` and as the body of this issue.

## Acceptance criteria

- [ ] Every row has: packages, text MB, measured or estimated resident MB, user impact, mechanism, owner to ask
- [ ] The three largest rows measured with a real build (size + service PSS at login screen)
- [ ] Rows ordered by resident MB per unit of user impact; a recommended cut line
- [ ] Product sign-off recorded per row before any removal PR is opened

## Blocked by

F04 (level 1), so the list is about features only.
