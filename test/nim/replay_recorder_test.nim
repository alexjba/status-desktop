## Record mode is opt-in (docs/replay-mode.md): without STATUS_RECORD the recorder library is
## never loaded and the nimqml hooks stay nil; with it, the library is loaded and hooks installed.

import unittest, os, strutils, sequtils
import nimqml
import app/core/replay_recorder
from seaqt/qcoreapplication import QCoreApplication, create

when defined(macosx):
  proc dyldImageCount(): uint32 {.importc: "_dyld_image_count", header: "<mach-o/dyld.h>".}
  proc dyldImageName(i: uint32): cstring {.importc: "_dyld_get_image_name", header: "<mach-o/dyld.h>".}

  proc loadedLibraries(): seq[string] =
    for i in 0'u32 ..< dyldImageCount():
      result.add $dyldImageName(i)
else:
  proc loadedLibraries(): seq[string] =
    for line in lines("/proc/self/maps"):
      result.add line

proc recorderLoaded(): bool =
  loadedLibraries().anyIt("StatusReplay" in it)

discard QCoreApplication.create()

suite "record mode loading":
  test "without STATUS_RECORD nothing is loaded and hooks stay nil":
    delEnv("STATUS_RECORD")
    check not recordingRequested()
    check not recorderLoaded()
    check contextPropertyHook == nil
    check metacallHook == nil
    check modelDataHook == nil

  test "with STATUS_RECORD the recorder is loaded and installs its hooks":
    # Set by the make rule to a stand-in library with the recorder's C entry points.
    let library = getEnv("STATUS_REPLAY_TEST_LIB")
    require library.len > 0
    putEnv("STATUS_RECORD", getTempDir() / "replay_recorder_test")
    putEnv("STATUS_REPLAY_LIB", library)
    check recordingRequested()
    startRecording(nil)
    check recorderLoaded()
    check contextPropertyHook != nil
    check metacallHook != nil
    check modelDataHook != nil
    setTraceHooks(nil, nil, nil)
