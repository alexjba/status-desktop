#!/usr/bin/env bash
# Runs a recording on Android with the replay variant (docs/replay-mode.md), called by
# `make mobile-replay-run`: pushes the recording, sets the options through status-env.txt,
# launches, and pulls the screenshot and slot-call log when REPLAY_QUIT_AFTER_MS is set.
set -euo pipefail

: "${APK:?replay apk to install}"
: "${PKG:?installed package name}"
: "${RECORDING:?recording directory}"
files="/sdcard/Android/data/$PKG/files"
device_recording="$files/recording"

adb install -r "$APK"

# Files adb creates in the app's external dir are only readable by the app when the app
# created that dir itself: start it once on a fresh install.
ensure_files_dir() {
    adb shell ls "$files" > /dev/null 2>&1 && return
    adb shell am start -n "$PKG/app.status.mobile.StatusQtActivity" > /dev/null
    sleep 5
    adb shell am force-stop "$PKG"
}
ensure_files_dir
adb shell am force-stop "$PKG"
adb shell rm -rf "$device_recording"
adb push "$RECORDING" "$device_recording" > /dev/null

{
    echo "STATUS_REPLAY=$device_recording"
    [ -n "${REPLAY_MOMENT:-}" ] && echo "STATUS_REPLAY_MOMENT=$REPLAY_MOMENT"
    echo "STATUS_REPLAY_MODE=${REPLAY_MODE:-realtime}"
    if [ -n "${REPLAY_QUIT_AFTER_MS:-}" ]; then
        echo "STATUS_REPLAY_QUIT_AFTER=$REPLAY_QUIT_AFTER_MS"
        echo "STATUS_REPLAY_SCREENSHOT=$files/replay.png"
    fi
} | adb shell "cat > $files/status-env.txt"

adb shell am start -n "$PKG/app.status.mobile.StatusQtActivity" > /dev/null
if [ -n "${REPLAY_QUIT_AFTER_MS:-}" ]; then
    sleep $(( REPLAY_QUIT_AFTER_MS / 1000 + 10 ))
    out="${REPLAY_OUTPUT:-$RECORDING/replay-android}"
    mkdir -p "$out"
    adb pull "$files/replay.png" "$out/" > /dev/null || echo "no screenshot"
    adb pull "$device_recording/replay-logs" "$out/" > /dev/null || true
    adb shell am force-stop "$PKG"
    echo "Replay output: $out"
fi
