# Status Mobile App Lifecycle

Vocabulary for how the mobile app behaves across background/foreground transitions, established while investigating wake-time UI stalls (issue #21395).

## Language

### Lifecycle

**Wake**:
The moment a live, backgrounded app instance returns to the foreground. Distinct from a warm restart — no loading screen, same process.
_Avoid_: resume, reopen (ambiguous with restart)

**Warm restart**:
A fresh UI-process launch while device state (caches, DBs, service process) is still warm. Shows the loading screen.

**Frozen**:
UI-process state under the Android cached-app freezer — no threads run, so nothing in-process can execute or log. Distinct from merely backgrounded.

**Backgrounded**:
UI process alive and schedulable but not visible; the Qt main loop is suspended, so queued events accumulate in-process.

**Paused services**:
status-go services that suspend their periodic work while the app is backgrounded and resume on wake. The service process itself stays fully alive.

### Wake-stall investigation

**Inactivity**:
Decomposes into three independently accumulating dimensions: message backlog, elapsed wall-clock time, and OS clamping (Doze/freezer). Saying "inactive for N hours" without naming the dimension is imprecise.

**Signal**:
A JSON event emitted by the service process to the UI process over the oneway Binder listener.

**Binder queue**:
The kernel-side async binder buffer (~1MB) holding signals sent to a frozen UI process. Overflow drops signals — a delivery failure, not a delay.

**Qt event queue**:
The in-process, unbounded queue where delivered signals wait while the app is backgrounded; drained in a burst on wake.

**Queued backlog**:
Signals emitted *before* the wake moment and drained at wake. Bounded by the binder queue cap.

**Fresh wake storm**:
Signals generated *because of* the wake itself — service resume, catch-up fetches. Unbounded; scales with inactivity.

**Token stall**:
The dominant wake-stall component: wallet token-service async completions (refresh-tokens, fetch-all-token-lists) parsed on the GUI thread. Signal-driven — triggered by `wallet.token-lists.updated`. Distinct from the backlog drain.

**Backlog drain**:
The secondary wake-stall component: processing the queued backlog through the Qt event queue at wake.

**Token catalogue**:
The full token universe returned by `getAllTokens` (~3MB). Fetched only when it can change — init or `wallet.token-lists.updated` — never on a routine refresh (upstream #21452).

**Routine refresh**:
A refresh-tokens run triggered by anything other than init/token-lists-updated. Refreshes tokens of interest only; skips the catalogue fetch.

**Known-missing key** (upstream: *unresolvable token key*):
A token key the backend confirmed as "not found" — typically a delisted spam/scam token. Negative-cached so lookups stop re-hitting `wallet_getTokensByKeys`; invalidated when a refresh applies fresh token data. Upstream #21452's `notFoundKeys` is the same concept; this series' `knownMissingKeys` implementation supersedes it.

### Wake benchmark

**Replay corpus**:
Real signal payloads captured at the emission point during an inactivity window plus the wake, stored as a fixture and re-injected for benchmark runs.

**Amplification**:
Offline cloning of corpus payloads to overnight volume with deterministic identity-field rewriting (message IDs, clocks). **Volume-type** signals (e.g. messages.new) are amplified; **trigger-type** signals (e.g. wallet.token-lists.updated, which fires expensive work per occurrence) are pinned at captured counts.

**Debug command signal**:
A synthetic signal (type `debug.command`) injected through the normal signal delivery path by a dev-build-only receiver, used to trigger benchmark actions in-app (e.g. fire the token refresh tasks).

**Wake-shaped run**:
A benchmark run that queues work while the app is backgrounded and measures from the wake moment — as opposed to a foreground fast-iteration run.
