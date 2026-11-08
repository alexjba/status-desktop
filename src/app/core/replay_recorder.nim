## Record mode (docs/replay-mode.md): with STATUS_RECORD set, loads libStatusReplay and
## installs its observers into nimqml. Without it nothing is loaded.

import std/[dynlib, os]
import chronicles
import nimqml

logScope:
  topics = "replay-recorder"

type
  RecorderHooks {.bycopy.} = object
    contextProperty: ContextPropertyHook
    metacall: MetacallHook

  AttachProc = proc(engine: pointer): RecorderHooks {.cdecl, gcsafe, raises: [].}
  MarkMomentProc = proc(name: cstring) {.cdecl, gcsafe, raises: [].}

var markMomentProc: MarkMomentProc

proc libraryPath(): string =
  result = getEnv("STATUS_REPLAY_LIB")
  if result.len > 0:
    return
  when defined(android):
    result = "libStatusReplay.so"
  elif defined(macosx):
    result = getAppDir() / "libStatusReplay.dylib"
  elif defined(windows):
    result = getAppDir() / "StatusReplay.dll"
  else:
    result = getAppDir() / "libStatusReplay.so"

proc recordingRequested*(): bool =
  existsEnv("STATUS_RECORD")

proc startRecording*(engine: pointer) =
  let path = libraryPath()
  let lib = loadLib(path)
  if lib == nil:
    warn "STATUS_RECORD is set but the recorder library is missing", path
    return
  let attach = cast[AttachProc](lib.symAddr("statusreplay_recorder_attach"))
  markMomentProc = cast[MarkMomentProc](lib.symAddr("statusreplay_recorder_markMoment"))
  if attach == nil:
    warn "recorder library has no attach entry point", path
    return
  let hooks = attach(engine)
  setTraceHooks(hooks.contextProperty, hooks.metacall)
  info "recording Nim to QML state", path

proc markRecordingMoment*(name: string) =
  if markMomentProc != nil:
    markMomentProc(name.cstring)
