# Go binary diet, level 2: sized feature-removal list for product sign-off

**Expected gain:** memory and binary size, service process. Measured rows (WebRTC, QUIC/WebTransport, news feed): −2.76MB text, −14MB `libstatus.so`, ≈−4.6MB resident, no mobile impact. Estimated further rows (relay/pubsub/gogo, NAT, seven product features): ≈−2.7MB text / −4.4MB resident, each needing product sign-off. This issue produces the decision list; the removals are separate PRs.

Context: status-im/status-app#21528. After the dead-code pass (level 1) the remaining size of the mobile Go library
is features: libp2p transports and services the mobile client may not need (WebRTC via `pion/*` ~0.9MB text, QUIC
~0.7MB incl. `gojay` qlog, DNS/mDNS discovery), go-waku store/filter/lightpush code paths, wallet providers and
contract bindings (`internal/contracts/*` ~0.5MB), community token deployer bytecode in rodata, stickers, ENS name
wrapper, pairing, connector (dapp bridge), push-notification client, RSS/news, shared URLs, and the two protobuf
runtimes. Each is a product decision, not an engineering one.

## What to build

Sized list, unstripped Android `libstatus.so` (`go1.26`, tags `gowaku_no_rln nowatchdog disable_torrent`), text
attributed with `go tool nm -size` and grouped by feature (`profiles/floor/f05-text-by-feature.txt`, import chain per
package). Baseline: file 155.8MB, text 28.04, pclntab 24.09, rodata 15.51, `.data.rel.ro` 9.61, `.rela.dyn` 8.97.
Three rows were **measured** by rebuilding the library without the feature (`f05-readelf-*.txt`, `f05-text-diff-*.txt`,
patches and build script in `f05-experiment-patches.txt`). Resident MB uses the login-screen residency ratios from
`floor.md`: resident ≈ 0.74×Δtext + 0.58×Δpclntab + 1.0×Δ`.data.rel.ro`; for estimated rows that ratio came out at
1.6–1.7× text on all three measured builds, so estimated resident = 1.65×text.

| # | feature | packages (MB text) | MB text | resident MB | mechanism | user impact on mobile | owner to ask |
|---|---|---|---|---|---|---|---|
| 1 | **libp2p WebRTC transport** (measured) | `pion/*` 79 pkgs 1.23, `go-libp2p/p2p/transport/webrtc` 0.05 | **1.31** (file −6.5, pclntab −1.17, relro −0.46) | **2.1** | go-waku never configures WebRTC; it is linked only because `go-libp2p/defaults.go` lists it in `DefaultTransports` and `basic_host.go` calls `IsWebRTCDirectMultiaddr`. Patch go-libp2p (3 files + a copied multiaddr helper, `f05-experiment-patches.txt` A) behind a `nowebrtc` build tag, upstream or carry in the waku-org fork. 0.1MB of `pion/dtls/v2`+`stun` stays via `go-ethereum/p2p/nat` ← `node` ← `services/personal` (F04 row) | none: `node.DefaultLibP2POptions` is tcp+quic+webtransport, no `webrtc-direct` addresses anywhere in the fleet | Waku team (go-libp2p fork); no product decision |
| 2 | **libp2p QUIC + WebTransport** (measured, on top of 1) | `quic-go` 0.52, `gojay` (qlog) 0.23, `webtransport-go`+`transport/webtransport` 0.07, `quicreuse`+`transport/quic` 0.05, `klauspost/compress` zstd/huff0/fse 0.13, `x/net` bpf/ipv4 0.04 | **1.11** (file −5.8, pclntab −1.12, relro −0.40) | **1.9** | go-waku `DefaultLibP2POptions` adds `quic.NewTransport` and `webtransport.New` (`wakuoptions.go`); go-libp2p `defaults.go`, `config/config.go` (stateless-reset keys, `quicreuse.ConnManager`, swarm dependency), `options.go` (`QUICReuse`), `basic_host.go` (`IsWebtransportMultiaddr`). Patch both behind a `noquic` tag (patches B). Measured together with 1; the row is the increment | mobile runs as a Waku light client (`wakuV2LightClient: IS_MOBILE`) and never listens; fleet store/filter/lightpush nodes are `/dns4/…/tcp/30303` or `enrtree://` ENRs (go-waku ENR has no QUIC field), so no connection uses QUIC today. Risk only if a future fleet or peer-exchange peer advertises QUIC-only addresses | Waku team (fleet transport policy) |
| 3 | **Status News RSS feed** (measured) | `json-iterator` 0.19, `modern-go/reflect2` 0.04, `gofeed`+`goxpp` 0.09, `goquery`+`cascadia` 0.05, `x/net/html` share; status-go `pkg/services/newsfeed`+`internal/newsfeed` 0.015 | **0.34** (file −1.8, pclntab −0.34, relro −0.12) | **0.57** | measured by replacing the `gofeed` module with a stub; the real mechanism is a `nonewsfeed` tag on `pkg/services/newsfeed` + `internal/newsfeed` (stub service, no `gofeed` import) and dropping the blank `_ "github.com/mmcdole/gofeed"` import in `internal/protocol/messenger.go` | "Status News" notifications in the Activity Center stop (`newsFeedEnabled`/`newsRSSEnabled` settings, `PrivacyStore.isStatusNewsViaRSSEnabled` toggle must be hidden on mobile) | product: Activity Center / comms |
| 4 | **Waku relay + libp2p-pubsub + gogo protobuf** (estimated) | `go-libp2p-pubsub`+`/pb` 0.35, `gogo/protobuf` 0.57 (only pubsub uses gogo), go-waku `protocol/relay` 0.04 | 0.96 | 1.6 | needs a go-waku fork: `WakuNode.Relay()` is wired unconditionally in `node/wakunode2.go` and used by peermanager/filter-full-node paths; a `norelay` tag with a nil relay and a status-go gate in `pkg/messaging/waku/gowaku.go` (`node.WithWakuRelay` only when `!LightClient`). Largest remaining Waku row, highest effort | removes the Advanced-settings "Waku light client" toggle on mobile: the app can only run as a light client (its default). Communities history/archives unaffected (store) | Waku team + product (settings) |
| 5 | **libp2p NAT/relay/autonat/hole-punching services** (estimated) | `circuitv2`+`autorelay` 0.11, `go-nat`+`goupnp`+`nat-pmp`+`p2p/net/nat` 0.10, `autonat`/`autonatv2` 0.08, `holepunch` 0.03 | 0.32 | 0.5 | go-waku enables `EnableNATService()` and `EnableHolePunching()` in `DefaultLibP2POptions`; status-go adds `libp2p.NATPortMap()` (`gowaku.go:291`). Turning the options off does not unlink the code (go-libp2p `config.go`/`options.go` import them); needs the same fork tags as rows 1–2. Do after 1–2 | none for a light client that accepts no inbound connections (UPnP/NAT-PMP mapping, autonat probes and relay reservations are wasted work and network traffic today) | Waku team |
| 6 | **Stickers** (estimated) | `internal/contracts/stickers` 0.13, `olympos.io/encoding/edn` 0.08, `pkg/services/stickers` 0.02 | 0.23 | 0.4 | `nostickers` tag on the service registration in `pkg/backend/node` (service + contract binding + edn parser); the Nim `stickers_service` is unconditional in `src/app/boot/app_controller.nim` and the chat section wires `stickersPopupLoader` (`ui/app/mainui/sectionLoaders/ChatLoader.qml`), so mobile also needs a Nim/QML gate | sticker picker and sticker-pack market gone; received sticker messages still render (image by hash via the media server) | product: chat |
| 7 | **ENS usernames** (estimated) | `wealdtech/go-multicodec` 0.10 (an `init.0` of 102KB building a table in code), `go-ens` 0.01, contracts `namewrapper`/`registrar`/`snt`/`resolver`/`universalresolver` 0.11, `pkg/services/ens` 0.01, `pendingtxtracker`+`squirrel` 0.11 shared with wallet | 0.24 | 0.4 | `noens` tag on `pkg/services/ens` + `internal/protocol/ens`; contract bindings follow. The multicodec table is better fixed as code→data (level 1) than removed | ENS username registration/release and ENS verification in profile; ENS name resolution in the send flow falls back to addresses only | product: profile + wallet |
| 8 | **Wallet: swap/bridge router** (estimated) | `wallet/router`+`pathprocessor`+`routes`+`fees` 0.21, `thirdparty/paraswap`+`lifi` 0.02, contracts `hop/*` 0.03, `uniswap` 0.01 | 0.27 | 0.45 | tag on the router registration in `pkg/services/wallet` (`NewService`) + Nim/QML gating of the swap/bridge modals (`popupRequestsHandler.swapModalHandler`) | no swap, no bridge, send limited to same-chain transfers | product: wallet |
| 9 | **Wallet: collectibles** (estimated) | `wallet/collectibles`+`ownership` 0.13, `thirdparty/collectibles/{rarible,alchemy}` 0.05, contracts `erc721`/`ierc1155`/`cryptokitties` ~0.01 | 0.19 | 0.3 | tag on the collectibles manager + Nim collectibles module gating | no collectibles tab, no NFT send, community collectibles not shown | product: wallet |
| 10 | **Wallet: activity + leaderboard + market + onramp** (estimated) | `wallet/activity`+`activityfetcher`+`thirdparty/activity` 0.09, `leaderboard` 0.035, `market`+`coingecko` 0.03, `onramp` 0.002 | 0.16 | 0.26 | per-feature tags in `pkg/services/wallet` + Nim gating; each is independent | activity: no transaction history; leaderboard: no token leaderboard; market: no prices/charts (balances still shown in crypto); onramp: no buy-crypto links | product: wallet |
| 11 | **Community tokens (minting/airdrops)** (estimated) | `pkg/services/communitytokens` 0.03, contracts `community-tokens/*` 0.17 (+57KB deployer bytecode in rodata), `internal/contracts/community-tokens/deployer` is also imported by `pkg/services/networks` | 0.20 | 0.33 | tag on `communitytokens` service + move the deployer address table out of `pkg/services/networks` | owners/admins cannot mint, airdrop or burn community tokens from mobile; holding and permission checks unaffected (done by `communities`) | product: communities |
| 12 | **WalletConnect / dapp connector** (estimated) | `pkg/services/connector`+`walletconnect`+`chainutils`+`database` 0.11 | 0.11 | 0.18 | tag on the connector service in `pkg/backend/node`; Nim `app_service/service/connector` + `src/backend/connector.nim` gating | no WalletConnect sessions (dapp sign/send requests) on mobile | product: wallet |
| 13 | keep: shared URLs/brotli 0.25 (status.app deep links), local pairing 0.09 (sync with desktop), push-notification client 0.07 (`registerForPushNotifications`, iOS), go-waku store/filter/lightpush/peer-exchange 0.16 (the light client itself), `miekg/dns` 0.28 (`/dns4` multiaddr resolution), images 0.22, backup 0.01, browser permissions 0.005, keycard (no Go code linked) | | ~1.1 | | not removable without losing core mobile behaviour | | |
| F04 | **not features, already level 1** (this pass sharpened the numbers): `go-ethereum/node` via `services/personal` is **2.9MB** text, not 1.3: pebble+goleveldb+rawdb 1.54 **+ 1.03MB of C `ZSTD_*` (DataDog/zstd cgo ← pebble/sstable)** + `urfave/cli`+`blackfriday`+`go-md2man` 0.14 (← `go-ethereum/internal/debug`) + `p2p/nat`+pion stun/dtls v2 0.1; OTLP/grpc 0.84; gofakeit+testify+yaml 0.47; kzg/gnark 0.32; token-list validators/jsonschema 0.24; sentry 0.12; `lib/pq` 0.11 (postgres driver, `internal/protocol/message_persistence.go`); prometheus 0.15 (`internal/metrics` ← `pkg/backend`) | | 5.2 | 8.6 | build tags / dependency replacement | none | engineering |
| L1+ | **code→data, new level-1 items**: `extkeys.NewMnemonic` is a single 424KB function (eight BIP-39 word lists as composite literals rebuilt on every call); `go-multicodec.init.0` 102KB. Make them package-level vars/embedded tables: ~0.5MB text → ~0.25MB rodata | | 0.53 | ~0.6 | status-im/extkeys + wealdtech PR or local copy | none | engineering |

Measured rows 1–3 together: file −14.1MB (155.8 → 141.7), text −2.76, pclntab −2.63, `.data.rel.ro` −0.98,
`.rela.dyn` −0.88; resident ≈ **−4.6MB** on the service process at the login screen (section deltas in
`f05-section-deltas.txt`; the three builds compose: 2 was built on top of 1 and 3 is independent).

**Recommended cut line.** Do rows 1, 2 and 5 (Waku transport/NAT services: 2.7MB text, ≈4.5MB resident, no
user-visible change for a light client; one patched go-libp2p + go-waku, owned by the Waku team) and row 3 (news feed,
0.34MB, needs only a product yes/no). Row 4 (relay/pubsub/gogo, ≈1.6MB resident) is the next-largest but is a go-waku
fork of the node wiring and removes the light-client toggle: ask for it once the first two land. Rows 6–12 (wallet,
stickers, ENS, community tokens, connector) are 0.1–0.3MB text each, 1.4MB text / ≈2.3MB resident in total if all were removed; every
one is a mobile feature that product has to give up explicitly and each also needs a Nim/QML gate, so they sit below the
line unless product already plans a reduced mobile wallet. Everything in row 13 stays.

**Total achievable.** Measured: 2.76MB text / ≈4.6MB resident for WebRTC + QUIC + news. Estimated on top: 0.32 (NAT
services) + 0.96 (relay/pubsub/gogo) ≈ 1.3MB text / 2.1MB resident with no product impact beyond the light-client
toggle, and up to ≈1.4MB text / 2.3MB resident of product-owned wallet/chat/profile features. Level 2 therefore tops out
around **5.5MB text / ≈9MB resident** against a 28MB text / ~58MB resident baseline, and the Waku transport rows are
the only ones that are both large and impact-free; the F04 level-1 rows (5.2MB text, now including 1MB of zstd C code)
remain the bigger and safer cut.

## Acceptance criteria

- [ ] Every row has: packages, text MB, measured or estimated resident MB, user impact, mechanism, owner to ask
- [ ] The three largest rows measured with a real build (size + service PSS at login screen)
- [ ] Rows ordered by resident MB per unit of user impact; a recommended cut line
- [ ] Product sign-off recorded per row before any removal PR is opened

## Blocked by

F04 (level 1), so the list is about features only.
