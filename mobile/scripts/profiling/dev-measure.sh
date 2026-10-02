#!/bin/bash
# dev-measure.sh <label> — iPad warm-login run: relaunch (MallocStackLogging=lite), log in through WebDriverAgent,
# settle 2 min on the wallet, pull Go heap + VM summary each 30s, then a live malloc dump. Appends to devmem/ledger.csv.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
D=00008101-000474143446001E; LOGS="Library/Application Support/Status/logs"; L=$1; OUT=$S/ios/devmem
pull() { xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier app.status.mobile --source "$LOGS/$1" --destination "$2" >/dev/null 2>&1; }
$S/ios/dev-run.sh >/dev/null; sleep 15
$S/ios/devsnap.sh $L-login-screen | cut -c1-160
python3 $S/ios/wda.py launch >/dev/null; sleep 2
python3 $S/ios/wda.py find "TextField" | head -2
python3 $S/ios/wda.py tapxy 410 649; sleep 1; python3 $S/ios/wda.py text "TestPassword2026" >/dev/null; sleep 1
python3 $S/ios/wda.py tap "Log In"; T0=$(date +%s)
sleep 20; $S/ios/devsnap.sh $L-postlogin-20s | cut -c1-160
sleep 25; $S/ios/devsnap.sh $L-postlogin-45s | cut -c1-160; pull vm-summary-latest.txt $OUT/$L-vm-45s.txt
for i in 1 2 3 4; do sleep 30; $S/ios/devsnap.sh $L-settle-$((i*30))s | cut -c1-160; pull vm-summary-latest.txt $OUT/$L-vm-settle-$((i*30))s.txt; pull go-heap-latest.pb.gz $OUT/$L-heap-settle-$((i*30))s.pb.gz; done
echo "footprint series (15s):"; grep memfootprint $S/ios/dev-app-latest.log | sed 's/.*phys_mb=\([0-9]*\).*/\1/' | tr '\n' ' '; echo
$S/ios/malloc-dump.sh $L-settle 25 | head -60
