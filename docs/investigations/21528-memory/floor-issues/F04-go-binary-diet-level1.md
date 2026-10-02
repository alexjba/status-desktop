# Go binary diet, level 1: dead code and build configuration on mobile

**Expected gain:** memory and binary size, service process. Dead code removal: ≈3–4MB of Go text (node framework incl. pebble + 1MB of C zstd 2.9, testify/gofakeit 0.75, otel+grpc 0.7, sentry/pq/prometheus 0.4, kzg 0.27) → ≈−6..−8MB resident (resident ≈ 1.65× text, from the F05 rebuilds) and ≈−18..−22MB of `libstatus.so` (APK). Partly measured (ratio), rows estimated from symbol sizes.

Context: status-im/status-app#21528. The Android `libstatus.so` is 84MB (text 25.7, pclntab 22.8, rodata 14.8,
`.data.rel.ro` 9.2 + `.rela.dyn` 8.6 for relocations). At the login screen 57.8MB of it is resident
(text 19, pclntab 13.3, relocated data 8.9 dirty, rela 8.5, rodata 6.3). Resident pages scale with binary size:
every MB of Go text removed takes ~1MB of pclntab and ~0.4MB of relocation data with it.

An unstripped Android build (2026-10-02) attributes the 26.7MB of text; the following link code that the mobile client
never exercises, with the import chain that pulls it in:

| MB text | package | pulled in by |
|---|---|---|
| 1.0 + 0.3 | `cockroachdb/pebble`, `syndtr/goleveldb`, go-ethereum `core/rawdb`/`ethdb` | `go-ethereum/node` ← `pkg/services/personal` (a full node framework for a signing service) |
| 0.3 + 0.2 | `stretchr/testify` (+ `yaml.v3`) | `internal/protocol` imports the suite in non-test code |
| 0.29 | `brianvoe/gofakeit/v7` | `internal/protocol/communities` |
| 0.29 + 0.4 | `go.opentelemetry.io/*`, `google.golang.org/grpc` | `internal/instrumentation/trace` (OTLP gRPC exporter) ← `internal/protocol` |
| 0.27 | `consensys/gnark-crypto` bls12-381 | go-ethereum `crypto/kzg4844` ← `signer/core/apitypes` ← `mobile` (EIP-712 only needs the types) |
| 0.12 | `getsentry/sentry-go` | `pkg/sentry` ← `callog` ← `mobile` |
| 0.18 | `mmcdole/gofeed` + `json-iterator` | `internal/protocol` (RSS) |
| 0.22 | `andybalholm/brotli` | `pkg/services/sharedurls` |
| 0.12 + 0.12 | `go-playground/validator`, `xeipuuv/gojsonschema` | go-wallet-sdk token types/fetcher |
| metrics | go-ethereum metrics registries | `-X …metrics.EnabledStr=true` in the Android link |

Plus: `gogo/protobuf` (0.55) and `google.golang.org/protobuf` (0.52) both linked (libp2p-pubsub vs. our protobufs).

## What to build

One PR per row (status-go `develop`, mobile tags only where a build tag is needed): replace `go-ethereum/node` in
`services/personal` with the signing code it uses; move testify/gofakeit behind `_test.go` or a build tag; make the
OTLP trace exporter opt-in via tag; drop the kzg dependency path for typed-data signing; metrics off on mobile;
sentry/gofeed/brotli behind tags if they are desktop-only. Measure the unstripped binary (`go tool nm -size` by
package, script in `mobile/scripts/profiling/floor/`) and the resident pages after each PR.

## Acceptance criteria

- [ ] Per-PR row: libstatus.so size, text/pclntab/data.rel.ro sizes, service PSS at login screen and settle (warm/cold)
- [ ] No functional change on mobile; desktop unaffected or explicitly tagged
- [ ] Combined: ≥ 3MB of text removed (≈ −8MB file, ≈ −5MB resident) before level 2 starts

## Blocked by

Nothing.
