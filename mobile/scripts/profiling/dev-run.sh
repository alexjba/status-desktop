#!/bin/bash
# dev-run.sh [install] — install/launch the Status app on the iPad with console capture; starts iproxy for pprof.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
DEV=00008101-000474143446001E
APP=${APP:-/Users/alexjbanca/Repos/status-desktop.worktrees/perf-21528-memory/mobile/bin/ios/qt6/Status.app}
BID=app.status.mobile
if [ "${1:-}" = install ]; then xcrun devicectl device install app --device $DEV "$APP" 2>&1 | tail -2 || exit 1; fi
pkill -f "iproxy 6060" 2>/dev/null; (nohup iproxy 6060 6060 -u $DEV > $S/ios/iproxy.log 2>&1 < /dev/null &)
TS=$(date +%H%M%S)
(nohup xcrun devicectl device process launch --console --terminate-existing --device $DEV $BID > $S/ios/dev-app-$TS.log 2>&1 < /dev/null &)
sleep 4; echo "console log: $S/ios/dev-app-$TS.log"; ln -sf $S/ios/dev-app-$TS.log $S/ios/dev-app-latest.log
