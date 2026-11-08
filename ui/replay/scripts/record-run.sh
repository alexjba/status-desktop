#!/usr/bin/env bash
# Unattended desktop recording (docs/replay-mode.md), called by `make record-run`.
#
# Uses its own data dir with a throwaway test profile: creates it through onboarding on the
# first run (create-profile.json), then records a run that starts at the login screen, logs
# in and opens the wallet (login.json). RECORD_SCRIPT=none records without driving the UI.
set -euo pipefail

: "${MAKE:=make}"
: "${STATUS_REPLAY_LIB:?libStatusReplay path}"
: "${RECORDING:?recording directory}"
: "${RECORD_DATADIR:?data dir for the test profile}"
scripts="$(cd "$(dirname "$0")" && pwd)"
RECORD_SCRIPT="${RECORD_SCRIPT:-$scripts/login.json}"
export STATUS_REPLAY_LIB

run_app() { # <recording dir> <script> <quit after script ms>
    STATUS_RECORD="$1" STATUS_RECORD_SCRIPT="$2" STATUS_RECORD_QUIT_AFTER_SCRIPT_MS="$3" \
        "$MAKE" run ARGS="--dataDir=$RECORD_DATADIR"
}

has_profile() {
    compgen -G "$RECORD_DATADIR/data/0x*-v4.db" > /dev/null
}

has_moments() { # <recording dir> <moment>...
    local manifest="$1/manifest.json"
    shift
    for moment in "$@"; do
        grep -q "\"name\": \"$moment\"" "$manifest" 2>/dev/null || { echo "missing moment '$moment' in $manifest"; return 1; }
    done
}

if ! has_profile; then
    echo "Creating the test profile in $RECORD_DATADIR"
    run_app "$RECORD_DATADIR/onboarding-recording" "$scripts/create-profile.json" 5000
    has_profile || { echo "onboarding did not create a profile (see the app log)"; exit 1; }
fi

if [ "$RECORD_SCRIPT" = "none" ]; then
    STATUS_RECORD="$RECORDING" "$MAKE" run ARGS="--dataDir=$RECORD_DATADIR"
else
    rm -rf "$RECORDING"
    run_app "$RECORDING" "$RECORD_SCRIPT" "${RECORD_TAIL_MS:-10000}"
    has_moments "$RECORDING" startup loggedIn exit
    # A change to the Nim startup that the replay setup does not mirror fails here.
    if [ -n "${STATUS_REPLAY_BINARY:-}" ]; then
        "$STATUS_REPLAY_BINARY" --recording "$RECORDING" --check-setup || {
            echo "replay setup differs from the recorded app: update ui/replay/app/replaysetup.cpp"
            exit 1
        }
    fi
fi
echo "Recording: $RECORDING"
