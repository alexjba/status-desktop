#!/usr/bin/env bash
# Unattended Android recording (docs/replay-mode.md), called by `make mobile-record-run`.
#
# Same flow as record-run.sh, on a device: creates a test profile through onboarding when the
# package has none, then records login screen -> login -> wallet and pulls the recording.
# Everything runs in-process (STATUS_RECORD_SCRIPT); the env reaches the app through the
# status-env.txt file that debug, PR and replay packages read at start.
set -euo pipefail

: "${APK:?apk to install}"
: "${PKG:?installed package name}"
: "${RECORDING:?local directory for the recording}"
scripts="$(cd "$(dirname "$0")" && pwd)"
RECORD_SCRIPT="${RECORD_SCRIPT:-$scripts/login.json}"
files="/sdcard/Android/data/$PKG/files"
activity="$PKG/app.status.mobile.StatusQtActivity"

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

# Debuggable packages are checked with run-as. Non-debuggable (release) ones are asked: a short
# scripted run reaches the login screen, and marks 'profileFound', only with a profile.
has_profile() {
    if adb shell run-as "$PKG" true > /dev/null 2>&1; then
        adb shell run-as "$PKG" find . -name "'*-v4.db'" 2>/dev/null | grep -q -- '-v4.db'
        return
    fi
    run_app "$files/profile-check" "$scripts/has-profile.json" 1 180
    adb shell "grep -q '\"name\": \"profileFound\"' $files/profile-check/manifest.json 2>/dev/null"
}

# <device recording dir> <script> <quit after script ms> <timeout s>
run_app() {
    local device_dir="$1" script="$2" quit_after="$3" timeout="$4"
    adb shell am force-stop "$PKG"
    adb push "$script" "$files/record-script.json" > /dev/null
    printf 'STATUS_RECORD=%s\nSTATUS_RECORD_SCRIPT=%s\nSTATUS_RECORD_QUIT_AFTER_SCRIPT_MS=%s\nSTATUS_RECORD_SETTLE_MS=%s\nSTATUS_RECORD_STARTUP_DELAY_MS=%s\n' \
        "$device_dir" "$files/record-script.json" "$quit_after" "${STATUS_RECORD_SETTLE_MS:-20000}" \
        "${STATUS_RECORD_STARTUP_DELAY_MS:-5000}" \
        | adb shell "cat > $files/status-env.txt"
    adb shell rm -rf "$device_dir"
    adb shell am start -n "$activity" > /dev/null
    local waited=0
    until adb shell "grep -q '\"name\": \"exit\"' $device_dir/manifest.json 2>/dev/null"; do
        sleep 5
        waited=$((waited + 5))
        if [ "$waited" -ge 30 ] && [ -z "$(adb shell pidof "$PKG")" ]; then
            echo "app exited without an exit moment"
            break
        fi
        if [ "$waited" -ge "$timeout" ]; then
            echo "no exit moment after ${timeout}s"
            break
        fi
    done
    adb shell rm -f "$files/status-env.txt"
    adb shell am force-stop "$PKG"
}

if ! has_profile; then
    echo "Creating the test profile in $PKG"
    run_app "$files/onboarding-recording" "$scripts/create-profile.json" 5000 600
    has_profile || { echo "onboarding did not create a profile (adb logcat)"; exit 1; }
fi

run_app "$files/recording" "$RECORD_SCRIPT" "${RECORD_TAIL_MS:-10000}" 600
rm -rf "$RECORDING"
mkdir -p "$(dirname "$RECORDING")"
adb pull "$files/recording" "$RECORDING" > /dev/null
for moment in startup loggedIn exit; do
    grep -q "\"name\": \"$moment\"" "$RECORDING/manifest.json" || { echo "missing moment '$moment'"; exit 1; }
done
echo "Recording: $RECORDING"
