# Settings subpages and sections referenced by URL (follow-up of the popups pilot)

**Expected gain:** memory after login. Settings subpages (4.2MB of units, loaded on first open) and sticky sections released after an unload policy: the remainder of F10's −10..−20MB range, measured as settle PSS after a full section tour. APK: none.

Context: status-im/status-app#21528. Same mechanism as the popups pilot, applied to the profile/settings subpages
(`AppLayouts/Profile`, 4.2MB of compiled units, 99% loaded once the section opens) and to the remaining section
loaders that still hold static `sourceComponent` references (`WalletLoader`, `HomePageLoader`, `ChatLoader`
skeletons are fine; the section contents are the target). Sticky sections (`CommunityChatLoader`, `ChatLoader`,
`BrowserLoader`) also need an unload policy before trim can reclaim them.

## What to build

Convert settings subpages to URL loading with trim on leave; define an unload policy for sticky sections (cap and
LRU) so a section not visited for N minutes is unloaded and trimmed; measure settle PSS after visiting every section
once and returning to the wallet, before/after.

## Acceptance criteria

- [ ] Ledger rows: settle PSS after a full section tour, before/after; engine heap via heapprofd
- [ ] Re-open latency of a trimmed section within the bench gate
- [ ] Unload policy documented in the guideline

## Blocked by

F10.
