# iOS leg (in progress, 2026-10-02)

Goal: same three steps as Android — baseline, post-login profile, top-10 offenders — on iOS. Single process:
status-go runs in-process, so one footprint number per run.

## Contract on iOS
- **Memory footprint** = `phys_footprint` from `task_info(TASK_VM_INFO)` (what jetsam acts on). The profiling build
  (`GO_PPROF=1`) logs it every 15s on stdout (`memfootprint phys_mb=… resident_mb=…`, `src/nim_status_client.nim`).
- Settle point, pre-login floor, startup burst: as in README (wallet section, 1–2 min idle).
- Go runtime: pprof served in-process on port 6060 (`0.0.0.0` on iOS so a Wi-Fi-paired device is reachable).

## Tooling
- Build: `IPHONE_SDK=iphoneos ARCH=arm64 GO_PPROF=1 make mobile-build …` (scratchpad `build-ios-device.sh`).
  Simulator: `IPHONE_SDK=iphonesimulator` (x86_64 only, see below).
- Device input: WebDriverAgent run directly with `xcodebuild test` over CoreDevice (Wi-Fi pairing, no usbmuxd), driven by
  a small WebDriver client (`wda.py`: tree/find/tap/text/screenshot). Simulator input: idb.
- Native attribution: Instruments via `xctrace --device`; host `vmmap`/`heap` cannot inspect simulator processes on
  this macOS at all.
- Snapshots: `mobile/scripts/profiling/iossnap.sh` (simulator: `footprint` + `vmmap` + pprof), `devsnap.sh` (device:
  footprint log line + pprof).

## Toolchain findings
- The documented simulator build is **x86_64**; on an Apple-silicon host the simulator device boots arm64 and runs the
  app under Rosetta. That adds ~200MB (Rosetta JIT/generic) to the footprint, blocks Instruments injection
  ("Failed to execute loader thread … liboainject"), and turns `__DATA_CONST` fully dirty (116MB of fixups).
- An arm64 simulator build does not link with the Qt 6.11.0 iOS kit: its `objects-Release/*.o` are device-only
  (`ld: building for 'iOS-simulator', but linking in object file … built for 'iOS'`). Patches made for it are kept
  (commonCmakeConfig honours `IPHONE_SDK`; status-go Makefile uses `-target …-simulator` and retags `go.o` with
  `vtool` because Go tags ios/arm64 objects as device).
- `vendor/nim-sds` iOS rule only knew the device SDK and passed `--cpu:x86_64` (Nim wants `amd64`): patched on local
  branch `perf/21528-ios-simulator` (pinned via `NIM_SDS_VERSION`, because status-go's `clone-nim-sds` does
  `git switch --force` + `git clean -fdx` on every build and wipes uncommitted changes).
- Nim's nimcache is not keyed on the clang target that `clangWrap.sh` injects through the environment: switching
  simulator↔device without `rm -rf nimcache` links the wrong objects.

## Simulator (x86_64 under Rosetta, iPhone 16 Pro / iOS 18.5) — reference only
| point | footprint MB | VM_ALLOCATE (Go) | MALLOC nano/small/medium | Rosetta | Go HeapSys / live |
|---|---|---|---|---|---|
| welcome screen (fresh install) | 420 | 23 | 26 / 34 / 57 | 97 | 27 / 17 |
| right after profile creation | 1577 | 557 | 293 / 182 / 136 | 194 | 540 / 106 |
| wallet settle 120s | 1660 | 556 | 351 / 182 / 158 | 195 | 540 / 39 |
Go live heap at settle 44.6MB (same offenders as Android: leveldb memdb 4MB, zxcvbn 3.7MB, token lists ~6MB, geth
metrics 2MB, gofakeit, blackfriday). Go `HeapReleased` 456MB but the pages stay in the footprint on the simulator —
whether `MADV_FREE_REUSABLE` drops the footprint on a real device is the first thing to check on the iPad.
Native malloc zones total ~720MB at settle, 351MB of it in the nano zone (≤256-byte objects) — not yet attributed.

## Device (iPad Air 4th gen, 4GB, iOS 26.5.2, Wi-Fi paired)
What works: install/launch with console capture (`devicectl … --console`), the 15s `memfootprint` log line, and two
files the profiling build writes to `Library/Application Support/Status/logs/` every minute and that `devicectl device
copy from` pulls: `go-heap-latest.txt` (Go heap profile, legacy text + MemStats, via `WriteHeapProfile`) and
`vm-summary-latest.txt` (in-process vmmap: footprint fields from `TASK_VM_INFO`, regions aggregated by VM user tag,
malloc zone statistics — `src/app/global/vm_summary.c`).
What does not: pprof over Wi-Fi (bound on 0.0.0.0:6060, connection refused — local-network privacy or similar; the
iPad is not on USB so there is no usbmuxd tunnel); `xctrace` Allocations export (the instrument's tables are not
exportable, local or device); WebDriverAgent (`Timed out while enabling automation mode`, needs Settings → Developer
→ UI Automation); host `vmmap`/`heap`/Instruments attach.

### Login screen, fresh launch (build 8, 2026-10-02)
| | MB |
|---|---|
| phys footprint | 197 (internal 107, external 165, compressed 0) |
| malloc heap `MALLOC_SMALL` (Qt + Nim + sqlcipher), 370k blocks in DefaultMallocZone | 75 |
| IOSurface (window render buffers, 3 × 2360×1640×4) | 45 |
| Go runtime (`untagged`/VM_ALLOCATE; Sys 29, HeapAlloc 15) | 30 |
| JavaScriptCore (QML JS heap) | 4.5 |
| GPU (IOAccelerator), stacks, CG | ~6 |
| mapped file resident (binary + frameworks, shared) | 810 (dirty 5) |
Android pre-login floor was ~330MB over two processes; iOS pays one process and no Dalvik/Binder, but the 45MB of
IOSurface is a tablet-resolution cost an iPhone would halve.
