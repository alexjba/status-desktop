# Status Mobile App Lifecycle

Vocabulary for how the mobile app behaves across background/foreground transitions, established while investigating wake-time UI stalls (issue #21395).

## Language

### External intake (share & link handling)

Established while designing OS share-sheet / default-browser integration (upstream #20439).

**External intake**:
Any content the OS hands to the app: a Status deep link, an arbitrary web URL, or shared content (text and/or images). The umbrella term for everything arriving through the platform intent/open-URL layer.

**Share target**:
The app's entry in the OS share sheet. Declaring content types there is a contract — only declare what chat can actually send.

**Direct-share shortcut**:
A recent postable destination published to the OS so it appears as a one-tap target in the share sheet, above the app row. Lives outside the app (name + avatar visible to the OS even while the app isn't running); must be cleared on logout.

**Send-message intent donation**:
The iOS analog of a direct-share shortcut: after each successful send, the destination (conversation id + name + avatar) is donated to the OS as an INSendMessageIntent, and iOS surfaces donated conversations as one-tap suggestion chips in the share sheet (the share extension declares INSendMessageIntent support). Same privacy rule: all donated interactions are deleted on logout; they repopulate organically as the user sends.

**Postable destination**:
A chat the logged-in user can post to: 1-1 chat, group chat, or community channel with post rights.

**Destination picker**:
The in-app screen where the user chooses a single postable destination for shared content — recency-sorted, searchable.

**Pending intake slot**:
The single, last-wins buffer holding an external intake until the app can act on it (`mainWindowReady`, i.e. after login or onboarding completes). Shared image streams are copied to app-private cache at receipt — the slot holds copied paths, never OS-managed URIs (their read grants expire).

**Browser candidacy**:
The app declaring it can handle arbitrary http/https links, making it appear in the OS link-chooser and default-browser lists. Every externally received web URL opens as a new browser tab; Status deep links keep their existing routing.

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
