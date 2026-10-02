#!/bin/bash
# f03-launch.sh <label> [VAR=VAL ...] — relaunch Status on the iPad with QSG_INFO + extra env, console log to f03-<label>.log
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad/ios
DEV=00008101-000474143446001E; BID=app.status.mobile; L=$1; shift
ENVS=(DEVICECTL_CHILD_QSG_INFO=1 "DEVICECTL_CHILD_QT_LOGGING_RULES=qt.scenegraph.general=true;qt.rhi.general=true")
for kv in "$@"; do ENVS+=("DEVICECTL_CHILD_$kv"); done
pkill -f "devicectl device process launch" 2>/dev/null; sleep 1
(nohup env "${ENVS[@]}" xcrun devicectl device process launch --console --terminate-existing --device $DEV $BID > $S/f03-$L.log 2>&1 < /dev/null &)
echo "launched $L env: $*  log: $S/f03-$L.log"; date +%s > $S/f03-$L.t0
