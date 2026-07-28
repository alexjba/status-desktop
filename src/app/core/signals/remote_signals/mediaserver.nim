import json

import base
import signal_type

type MediaServerStartedSignal* = ref object of Signal
  ## Emitted by status-go when the local media server (re)binds a port.
  ## On mobile the process suspension kills the listener and the restart
  ## picks a new ephemeral port, so every cached media URL goes stale.
  port*: int

proc fromEvent*(T: type MediaServerStartedSignal, jsonSignal: JsonNode): MediaServerStartedSignal =
  result = MediaServerStartedSignal()
  result.signalType = SignalType.MediaServerStarted
  if jsonSignal["event"].kind != JNull:
    result.port = jsonSignal["event"]{"port"}.getInt()
