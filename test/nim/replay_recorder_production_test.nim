## Production builds compile record mode out (docs/replay-mode.md): STATUS_RECORD is ignored and
## nothing is loaded. Built with -d:production by its make rule.

import unittest, os
import nimqml
import app/core/replay_recorder

suite "record mode in production builds":
  test "STATUS_RECORD is ignored":
    doAssert defined(production), "build this test with -d:production"
    putEnv("STATUS_RECORD", getTempDir() / "replay_recorder_production_test")
    putEnv("STATUS_REPLAY_LIB", getEnv("STATUS_REPLAY_TEST_LIB"))
    check not recordingRequested()
    startRecording(nil)
    check contextPropertyHook == nil
    check metacallHook == nil
    check modelDataHook == nil
