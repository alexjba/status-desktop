## App-lifecycle → status-go pausable-services bridge (iOS).
##
## On iOS status-go runs in-process and nothing drove PauseServices/
## ResumeServices: services never paused on backgrounding (battery cost) and —
## worse — never resumed on foregrounding, so the media server's listening
## socket iOS kills during suspension stayed dead and every cached
## https://localhost:<port>/ media URL failed until app restart. Resuming
## re-runs the whole recovery chain: ResumeServices →
## ServiceRegistry.ResumeMultiple → httpServer.ToForeground() → media server
## rebinds → mediaserver.started signal → the media-URL refresh.
##
## Wired only on iOS: the Android service process already drives pause/resume
## from binder UI-visibility changes (StatusGoService.java) — the UI process
## must not double-drive it — and desktop apps are never suspended.
##
## Flap handling: appBackgrounded is emitted only for Qt::ApplicationSuspended
## (a real backgrounding), never for the Inactive dips share sheets and system
## alerts cause; appForegrounded fires on EVERY return to Active, so resume
## runs only when a pause was actually driven (`paused` latch).
##
## Like the Android service, the service list is fetched from
## PausableServices() at each transition, so services registered in status-go
## later are picked up without client changes; an empty list (node not
## running, e.g. backgrounded on the login screen) drives nothing.

import nimqml, chronicles
import ./custom_urls/url_scheme_event

logScope:
  topics = "services-pause-bridge"

type PausableServicesCalls* = object
  ## Seam over the backend's pausable-services mobile API
  ## (backend/pausable_services), injectable for tests.
  pausableServiceNames*: proc(): seq[string]
  pauseServices*: proc(names: seq[string])
  resumeServices*: proc(names: seq[string])

QtObject:
  type ServicesPauseBridge* = ref object of QObject
    calls: PausableServicesCalls
    paused: bool

  proc delete*(self: ServicesPauseBridge) =
    self.QObject.delete

  proc onAppBackgrounded*(self: ServicesPauseBridge) {.slot.} =
    if self.paused:
      return
    let names = self.calls.pausableServiceNames()
    if names.len == 0:
      return
    self.paused = true
    info "app backgrounded, pausing services", names
    self.calls.pauseServices(names)

  proc onAppForegrounded*(self: ServicesPauseBridge) {.slot.} =
    if not self.paused:
      return
    self.paused = false
    let names = self.calls.pausableServiceNames()
    if names.len == 0:
      return
    info "app foregrounded, resuming services", names
    self.calls.resumeServices(names)

  proc setup(self: ServicesPauseBridge, urlSchemeEvent: UrlSchemeEvent) =
    self.QObject.setup
    # Both objects live on the main thread, so AutoConnection resolves to a
    # direct (synchronous) call — required on backgrounding: iOS freezes the
    # process soon after the state change is delivered, and a queued slot
    # might never run before suspension, leaving services un-paused.
    discard QObject.connect(urlSchemeEvent, SIGNAL("appBackgrounded()"),
      self, SLOT("onAppBackgrounded()"), ConnectionType.AutoConnection)
    discard QObject.connect(urlSchemeEvent, SIGNAL("appForegrounded()"),
      self, SLOT("onAppForegrounded()"), ConnectionType.AutoConnection)

  proc newServicesPauseBridge*(urlSchemeEvent: UrlSchemeEvent,
      calls: PausableServicesCalls): ServicesPauseBridge =
    new(result)
    result.calls = calls
    result.paused = false
    result.setup(urlSchemeEvent)
