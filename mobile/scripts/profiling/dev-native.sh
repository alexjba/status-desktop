#!/bin/bash
# dev-native.sh <label> — install the current build, warm login through WebDriverAgent, settle 2 min, then pull the
# live malloc blocks (with stacks) and symbolise them. Also dumps the stack-logging startup line.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
D=00008101-000474143446001E; L=$1
APP=${APP:-/Users/alexjbanca/Repos/status-desktop.worktrees/perf-21528-memory/mobile/bin/ios/qt6/Status.app}; xcrun devicectl device install app --device $D "$APP" >/dev/null 2>&1
$S/ios/dev-run.sh >/dev/null; sleep 15
grep -a -E "go pprof|malloc stack logging" $S/ios/dev-app-latest.log | head -3
$S/ios/devsnap.sh $L-login-screen | cut -c1-160
python3 $S/ios/wda.py launch >/dev/null; sleep 2
python3 $S/ios/wda.py tapxy 410 649; sleep 1; python3 $S/ios/wda.py text "TestPassword2026" >/dev/null; sleep 1
python3 $S/ios/wda.py tap "Log In"
sleep 125; $S/ios/devsnap.sh $L-settle-120s | cut -c1-160
echo "footprint series (15s):"; grep memfootprint $S/ios/dev-app-latest.log | sed 's/.*phys_mb=\([0-9]*\).*/\1/' | tr '\n' ' '; echo
$S/ios/malloc-dump.sh $L 30
