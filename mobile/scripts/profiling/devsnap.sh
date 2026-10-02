#!/bin/bash
# devsnap.sh <label> — iPad snapshot: last footprint line from the app console + the minute-ly Go heap dump pulled
# from the app container (pprof port is unreachable over Wi-Fi). Appends a CSV row.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
D=00008101-000474143446001E
OUT=${OUT:-$S/ios/devmem}; mkdir -p $OUT; TS=$(date +%Y%m%d-%H%M%S); L=${1:-snap}
fp=$(grep "memfootprint" $S/ios/dev-app-latest.log | tail -1 | sed 's/.*memfootprint //')
xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier app.status.mobile --source "Library/Application Support/Status/logs/go-heap-latest.txt" --destination $OUT/$TS-$L-heap.txt >/dev/null 2>&1
go=$(grep -E '^# (HeapAlloc|HeapSys|HeapIdle|HeapInuse|HeapReleased|Sys|NumGC) ' $OUT/$TS-$L-heap.txt 2>/dev/null | awk '{printf "%s=%d ", $2, ($2=="NumGC")?$4:$4/1048576}')
[ -f $OUT/ledger.csv ] || echo "ts,label,footprint,go_memstats" > $OUT/ledger.csv
echo "$TS,$L,$fp,$go" | tee -a $OUT/ledger.csv
