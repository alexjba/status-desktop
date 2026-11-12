# reclaim2: memory returned after closing the Browser, in-process vs out-of-process (Redmi A5, measured)

## Answer
- **A, in-process (master behaviour + destroy hook):** opening the Browser adds **+111 MB** to the UI process. Closing it (WebViews kept hidden) returns nothing. Destroying the Browser (unload + gc + trimComponentCache) returns **21 MB of 111 (19 %)**. Steps 2 to 4 (M_PURGE_ALL, onTrimMemory + System.gc, MADV_PAGEOUT) return about **0.4 MB more**. **+89 MB stays** in the UI process until the app restarts.
- **B, out-of-process (wp5, `:browser`):** opening the Browser adds **+131 MB on the device** (UI +3, `:browser` +125). Closing it kills `:browser` (gone in 3/3 runs at +60 s) and returns **117 MB of 131 (90 %)**. **+13.5 MB stays** in the UI process (native +9.5, Dalvik +6.5).
- So in-process destroy returns 21 of 111 MB. Out-of-process returns 117 of 131 MB. After closing, B keeps **~76 MB less** than A-destroyed.

## Setup
- A = `spike/floor-reclaim2` = 51c123c2cf + cherry-pick of 5ac46af23f (reclaim hooks) → b6cf462780 (GPG-signed). Package `app.status.freclaim`. pr release made debuggable, prebuilt Qt 6.11.0, stock mobilewebview pin. Built the same way as webproc's `rbuild.sh`, and the APK lib list matches wp5's.
- B = `webproc/apk/wp5.apk` (`app.status.fwebproc`), unchanged, existing test profile.
- Runs were interleaved A1 B1 A2 B2 A3 B3 on the Redmi only. A1 and B1 were re-run because of a broken baseline-check regex in the first pass (see below).
- Per run: fresh launch → login → wallet → +120 s **post** → Browser, type https://example.com in the tab (the tab first restores https://status.app in both builds, then goes to example.com) → +30 s **open** → Wallet → +60 s **hidden** (A) / **closed** (B) → A only: step 1 (+60 s) **S1**, step 2 **S2**, step 3 **S3**, step 4 **S4**, each +60 s.
- **Clean baseline checked in all 6 runs:** at post there were 0 WebView-provider mappings in UI smaps (`com.google.android.webview`, trichrome, `webviewchromium64.relro`, `app_webview`) and no `:browser` process. Every run ends on Wallet, so the next launch does not restore the Browser.

## Metric note (affects earlier reports)
On this device the `TOTAL PSS:` line of `dumpsys meminfo` **already includes SwapPss**: the per-category rows add up to TOTAL minus TOTAL SWAP PSS. `webproc/summ.py` and `webwin` compute "TOTAL = TOTAL PSS + TOTAL SWAP PSS", which counts swap twice. Their TOTALs are inflated by SwapPss, which was 0.5 to 59 MB in my snapshots. Below, **TOTAL = the `TOTAL PSS:` line** (PSS + SwapPss, counted once). Category columns are row Pss + SwapPss. Resident-only PSS moves around a lot as zram swaps the UI process when `:browser` starts, so compare TOTAL, not resident PSS.

## Results, MB, median of 3 (per-run values below)

| state | UI TOTAL | native | Dalvik | .apk | .so | EGL | Unknown | `:browser` | device sum | Δ device vs post |
|---|---|---|---|---|---|---|---|---|---|---|
| A post | 224.9 | 96.0 | 3.2 | 58.5 | 20.9 | 8.5 | 10.2 | n/a | 224.9 | 0 |
| A open | 333.8 | 128.0 | 12.0 | 99.3 | 22.1 | 16.4 | 19.9 | n/a | 333.8 | +105.3 |
| A closed-hidden (master) | 336.3 | 131.2 | 9.7 | 98.7 | 22.2 | 18.7 | 21.1 | n/a | 336.3 | **+111.0** |
| A S1 unload+gc+trim | 315.9 | 118.7 | 8.3 | 98.8 | 22.1 | 12.3 | 21.3 | n/a | 315.9 | **+89.6** |
| A S2 +M_PURGE_ALL | 315.6 | 118.5 | 8.0 | 98.8 | 22.0 | 12.3 | 21.7 | n/a | 315.6 | +89.3 |
| A S3 +onTrimMemory+gc | 315.3 | 118.5 | 7.8 | 98.8 | 22.0 | 12.3 | 21.7 | n/a | 315.3 | +89.0 |
| A S4 +MADV_PAGEOUT | 313.3 | 118.4 | 7.9 | 97.5 | 22.0 | 12.3 | 21.7 | n/a | 313.3 | **+89.2** |
| B post | 233.7 | 96.6 | 2.8 | 66.5 | 20.5 | 8.5 | 10.6 | none | 233.7 | 0 |
| B open | 237.9 | 104.4 | 6.9 | 63.7 | 13.3 | 11.5 | 11.4 | 124.7 | 364.5 | **+130.5** |
| B closed | 247.1 | 106.2 | 9.2 | 64.6 | 20.1 | 8.5 | 11.5 | none (killed) | 247.1 | **+13.5** |

The Δ column is the median of per-run deltas, so it is not always equal to the difference of the medians. GPU/GL mtrack is 0 on this Mali device; EGL mtrack is the graphics number.

Per run, UI TOTAL (device sum where `:browser` is up):
- A1: post 222.1 / open 326.6 / hidden 330.7 / S1 310.5 / S2 310.6 / S3 311.1 / S4 311.2
- A2: post 230.3 / open 335.6 / hidden 341.3 / S1 319.9 / S2 319.6 / S3 319.3 / S4 313.3
- A3: post 224.9 / open 333.8 / hidden 336.3 / S1 315.9 / S2 315.6 / S3 315.3 / S4 315.5
- B1: post 230.2 / open 230.6 + br 118.9 = 349.5 / closed 243.1
- B2: post 234.8 / open 237.9 + br 127.4 = 365.4 / closed 248.4
- B3: post 233.7 / open 239.8 + br 124.7 = 364.5 / closed 247.1

What stays in A after S4, from smaps (median Pss+SwapPss, S4 − post, `smapcat.txt`):
- WebView/Trichrome APK code +40.7. These pages are shared with other WebView users, so MADV_PAGEOUT cannot evict them.
- native heap +22.3. After unload it is 118.4, against 131.2 hidden and 96 at post.
- Chromium PartitionAlloc +9.9.
- other anon +4.0, Java +3.0, own .so +1.0.
- EGL +3.7 (mtrack). Unknown +11.4, which is Chromium/V8 anon in the dumpsys bucket.

This is the process-global WebView runtime: Android never unloads it once it is initialised.

`:browser` while open: native 28.7, PartitionAlloc 9.5, WebView code 36.7, Java 21.8, .so 12.3 → 118.5 in smaps (124.7 in dumpsys).

## Idle CPU, 60 s windows (ticks; 100 = 1 s of one core)

| | post | closed-hidden / closed | S1 | S4 | `:browser` |
|---|---|---|---|---|---|
| A (A1/A2/A3) | 3363 / n.a. / 3407 | **3793 / 3865 / 3771** | 3398 / 3452 / 3435 | 3358 / 3460 / 3436 | n/a |
| B (B1/B2/B3) | 3301 / 3368 / 3336 | 3342 / 3419 / 3288 | n/a | n/a | 0 (gone) |

- **Confound:** today the post-login baseline is ~56 % of one core in both builds. Yesterday's webwin runs saw ~2 %. `top -H` at post shows `QtThread` (scene-graph render thread) at 42 % plus `mali-cmar-backend` at 7 %, on a static Wallet screen showing the "POKT & Infura down" and "CoinGecko down" banners. Something renders continuously. This is environmental and affects A and B equally, but it needs its own follow-up (provider-down state → continuous rendering?). Deltas against post are still valid.
- A closed-hidden (master) costs **+~400 ticks/60 s ≈ +7 % of a core** over post, consistent with webwin's bleft 8.8 % vs 1.9 %. Destroy (S1) brings it back to post (+~30). B closed is about the same as post (+41 / +51 / −48), and `:browser` is gone.

## Open and reopen times (logcat markers: `TAP-Browser` logged in the same shell call as the tap, then `onPageFinished`)
- First open, cold engine, tap → restored tab finished: A 1.64 s and 1.69 s (in-process Chromium cold); B 1.98 s and 2.00 s (cold `:browser` process).
- Reopen after close:
  - **B:** cold `:browser`, tab restored. Tap → page finished in **1.26 s and 1.35 s**; screenrecord tap → settled frame 1.37 / 1.37 / 1.36 s (n=3).
  - **A-destroyed:** the Browser UI is back **0.20 s** after the tap (3/3), but **all tabs are lost** (0 tabs). In the hook build, the recreated Browser **could not load a page in 6/6 attempts**:
    - typing a URL in the empty address bar does nothing;
    - after "+", the New Tab view comes up with the keyboard open but no address field, and the typed text goes nowhere (screenshots in `dev/reopen/A*/`).
  - So A's tap → page-painted time is not measurable with this hook. The lower bound is the warm in-process URL → finished time of ~0.3 s (0.30 / 0.32 / 0.31 s in the open phase), plus the user re-opening their tabs. A shippable destroy would also need session restore and a fix for the broken re-create path.
- Warm URL → finished: A 0.30–0.34 s, B 0.34–0.45 s.

## Conclusion
- In-process destroy returns **21 of 111 MB (19 %)**. It drops idle CPU back to baseline, but it loses tabs, and in the spike the recreated Browser could not navigate.
- Out-of-process returns **117 of 131 MB (90 %)** and needs no extra steps. The UI process keeps +13.5 MB. A reopen costs ~1.3 s (cold `:browser` with tab restore) instead of 0.2 s plus re-creating tabs.
- Steps 2–4 (malloc purge, trim, pageout) are worth nothing in either design.
- While the Browser is open, B costs more on the device in total (+131 vs +105 MB), but the UI process itself grows only +3 MB. Under memory pressure, the system can kill `:browser` without killing the app.

## Risks / caveats
- n=3 per build. TOTAL noise between runs is ±5 MB at post. Deltas are consistent within ±3 MB.
- B's `MobileWebView` lib is from webproc's mobilewebview override (wp5), not the stock pin. Lib sizes differ by 1 KB.
- During the first pass, A1 and B1 were aborted by an over-broad baseline regex: `/system/lib64/libwebviewchromium_loader.so` is zygote-preloaded in every app. Both were re-run with the provider-only check, so all 6 reported runs are clean.
- The reopen-after-destroy runs (`dev/reopen/A1-3`, memory not measured) started with the Browser section restored, because the previous run's final Wallet tap missed with the keyboard up. That affects only those reopen-only runs, not the memory runs.

## Addendum (lead request): surviving threads + referenced bits (runs A4, A5, B4 in `dev/runs2/`)
- **Threads after destroy (A S1, `ps -T`, `S1.threads`):** 109 / 111 threads, of which ~31 are Chromium-owned and survive the destroy of every WebView:
  - Chrome_InProcGpuThread, Chrome_IOThread, Chrome_ChildIOThread, Chrome_ProcessLauncher, Chrome_DevToolsHandlerThread, VizWebView
  - NetworkService, CookieMonster ×4, ThreadPool* ×12 (Foreground 6, SingleThread 3, Service, Preset, Audio)
  - AudioThread, VideoCaptureThread, MemoryInfra, PerfettoTrace, SAFE_BROWSING, JavaBridge
- **B closed (`closed.threads`):** 64 threads, **0 Chromium threads**. The UI process is back to its own Qt/ART/Mali threads only.
- **Referenced bits: not measurable.** `run-as app.status.freclaim sh -c 'echo 1 > /proc/<pid>/clear_refs'` → `Permission denied` in 2/2 runs, so steps 2–3 were skipped as instructed. `S1-refs.smaps` was still captured, but its `Referenced` is cumulative since the mapping was created, so it cannot tell what was touched in the last 60 s. For reference, at S1 the WebView base.apk has 40 MB Rss, of which 29.6 MB is Referenced (r-xp 36.7 MB Rss / 26.3 MB Ref). A working route would be for the app to write `/proc/self/clear_refs` from the debug hook (needs a rebuild; not done).
- Memory in these runs (A4/A5, B4) shows the same direction but smaller A deltas: hidden +80, S1 +62, S4 +69; B closed +14. The device was under heavier memory pressure in the later session (UI SwapPss up to 53 MB), so file-backed WebView APK pages were evicted (`.apk` delta +10–19 vs +38 in runs 1–3). The destroy still returned ~18 MB in absolute terms. The main table uses runs 1–3 only. See `reclaim2/summ-runs2.txt`.

## Files
- Scripts: `reclaim2/{common,setup,run,rs,dev,redrun,rbuild,run-build}.sh`, `summ.py`, `smapcat.py`.
- Data:
  - `reclaim2/dev/runs/{A1..A3,B1..B3}/`: `.mi`, `.smaps` (UI and `-br`), `.gpu`, `.win`, png, mp4, `marks.txt`, `cpu.txt`, `time.txt`, `top-*.txt`
  - `reclaim2/dev/reopen/A1..A3/`
  - `reclaim2/summ.txt`, `reclaim2/smapcat.txt`
- APK A: `reclaim2/apk/rc1.apk`.
