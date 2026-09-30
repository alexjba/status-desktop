# Draft: golang/go issue — runtime: default madvdontneed=1 on android as on linux

**Title:** runtime: default to MADV_DONTNEED on android as well as linux

**What version of Go are you using?** go1.26.8 (also present in tip: `runtime/runtime1.go`, `parseRuntimeDebugVars`)

**What did you do?**
Built a `-buildmode=c-shared` library for `GOOS=android GOARCH=arm64` and loaded it in an Android
app process. After a large transient allocation (several hundred MB) the live heap dropped to ~70MB
and `runtime.MemStats.HeapReleased` reported ~350MB released, but the process RSS/PSS as seen by
`dumpsys meminfo`, `/proc/<pid>/status` and Android's memory UI stayed at the peak until the device
came under memory pressure.

**What did you expect to see?**
The same behaviour as on linux since Go 1.16 (#42330): scavenged pages leave RSS promptly, so
memory-based process management (Android's lmkd victim selection, the OS "memory resident apps"
UI, QA tooling) sees the real footprint.

**What did you see instead?**
`parseRuntimeDebugVars` only sets `debug.madvdontneed = 1` when `GOOS == "linux"`. For android
builds the runtime constant `GOOS` is `android`, so the default stays `MADV_FREE`
(`mem_linux.go: var adviseUnused = uint32(_MADV_FREE)`). #42330 explicitly cited an Android
report (#37569, `FreeOSMemory` not releasing on Android 9/10) as motivation, so the exclusion looks
unintended.

There is no practical override for an Android app: `//go:debug madvdontneed=1` is rejected as an
unregistered setting, and `GODEBUG` cannot be injected into a zygote-forked process before the
runtime initialises (the c-shared init reads the loader's original `envp`).

**Proposed change:** `if GOOS == "linux" || GOOS == "android" { debug.madvdontneed = 1 }` (or gate
on the linux build tag rather than the GOOS constant).

**Measurement** (Redmi A5, Android 15, status-go service process, fresh profile, 2 min after login):
PSS 354–569MB with `MADV_FREE`; 219MB with `MADV_DONTNEED` forced via `//go:linkname` to
`runtime.adviseUnused`; `HeapSys − HeapReleased` = 120MB in both cases.
