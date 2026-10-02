#!/bin/bash
# f03-run.sh <label> [VAR=VAL ...] — full variant run: launch, login-screen snapshot at 65s, WDA login, settle snapshot at t0+245s.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad/ios
L=$1; $S/f03-launch.sh "$@"; T0=$(cat $S/f03-$L.t0)
until [ $(date +%s) -ge $((T0+66)) ]; do sleep 3; done
python3 $S/wda.py shot f03-$L-login >/dev/null; $S/f03-pull.sh $L login
python3 $S/wda.py find "Serpentine" | head -1
python3 $S/wda.py tapxy 410 649 >/dev/null; sleep 1; python3 $S/wda.py text "TestPassword2026" >/dev/null; sleep 1; python3 $S/wda.py tap "Log In"
until [ $(date +%s) -ge $((T0+246)) ]; do sleep 3; done
python3 $S/wda.py shot f03-$L-settle >/dev/null; $S/f03-pull.sh $L settle
echo "series: $(grep memfootprint $S/f03-$L.log | sed 's/.*phys_mb=\([0-9]*\).*/\1/' | tr '\n' ' ')"
grep -i "atlas\|scenegraph\|qt.rhi\|swapchain\|glyph\|QRhi\|Metal" $S/f03-$L.log | sed 's/\x1b\[[0-9;]*m//g' | sed 's/.*text=//' | cut -c1-300 | head -40
