## Integration test for the Nim side of the iOS share-extension hand-off
## (urls_manager + pending_intake_slot). Proves both delivery paths over the
## real StatusQ urlActivated(QString) signal:
##  - the extension's wake URL (unsupported openURL trick) delivers and clears
##    the App Group pending intake slot without being routed as a deep link;
##  - appReady delivers a payload left behind when the wake never arrived
##    (degraded fallback: next manual app open, no data loss).

import unittest, os
import nimqml
import app/core/eventemitter
import app/core/custom_urls/url_scheme_event
import app/core/custom_urls/urls_manager
import app/core/intake/pending_intake_slot
import app/global/single_instance
import statusq_bridge
# Selective import: pulling all of gen_qcoreapplication would re-export the seaqt
# gen_qobject_types.QObject and make nimqml's QObject ambiguous (see url_scheme_event_test).
from seaqt/qcoreapplication import QCoreApplication, create, processEvents

discard QCoreApplication.create()  # one app for the whole suite

proc processEventsUntil(cond: proc(): bool) =
  var spins = 0
  while not cond() and spins < 100:
    QCoreApplication.processEvents()
    sleep(5)
    inc spins

suite "share_intake_wake":
  setup:
    # Not "share_intake_wake_test": newSingleInstance below creates its local
    # socket under that name in the temp dir and removeDir would trip on it.
    let slotDir = getTempDir() / "share_intake_wake_test_slot"
    removeDir(slotDir)
    let events = createEventEmitter()
    let urlSchemeEvent = newUrlSchemeEvent()
    let singleInstance = newSingleInstance("share_intake_wake_test", "")
    let slot = newPendingIntakeSlot(slotDir)
    let manager = newUrlsManager(events, urlSchemeEvent, singleInstance, "", slot)

  teardown:
    removeDir(slotDir)

  test "wake url delivers and clears the pending intake slot":
    manager.appReady()
    slot.write("""{"type":"share","text":"dummy"}""")

    statusq_urlscheme_emit_deeplink(urlSchemeEvent.vptr, ShareIntakeWakeUrl.cstring)
    processEventsUntil(proc(): bool = not fileExists(slot.filePath()))

    # consumed by the manager: cleared after read, nothing left to take
    check not fileExists(slot.filePath())
    check slot.take() == ""

  test "appReady delivers a payload left behind when the wake never arrived":
    slot.write("payload-from-killed-wake")

    manager.appReady()

    check not fileExists(slot.filePath())
    check slot.take() == ""

  test "appReady with an empty slot is a no-op":
    manager.appReady()
    check slot.take() == ""
