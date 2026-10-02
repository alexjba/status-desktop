#!/bin/bash
# devsnap.sh <label> — iPad snapshot: last footprint line from the app console + the minute-ly Go heap dump (pprof proto)
# and its MemStats sidecar pulled from the app container. Appends a CSV row.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
D=00008101-000474143446001E; LOGS="Library/Application Support/Status/logs"
OUT=${OUT:-$S/ios/devmem}; mkdir -p $OUT; TS=$(date +%Y%m%d-%H%M%S); L=${1:-snap}
pull() { xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier app.status.mobile --source "$LOGS/$1" --destination "$2" >/dev/null 2>&1; }
fp=$(grep "memfootprint" $S/ios/dev-app-latest.log | tail -1 | sed 's/.*memfootprint //')
pull go-heap-latest.pb.gz $OUT/$TS-$L-heap.pb.gz
pull go-heap-latest.pb.gz.memstats.json $OUT/$TS-$L-memstats.json
go=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(' '.join(f'{k}={v//1048576 if k!=\"NumGC\" and k!=\"Mallocs\" else v}' for k,v in d.items() if k in ('Sys','HeapAlloc','HeapSys','HeapIdle','HeapInuse','HeapReleased','NumGC')))" $OUT/$TS-$L-memstats.json 2>/dev/null)
[ -f $OUT/ledger.csv ] || echo "ts,label,footprint,go_memstats" > $OUT/ledger.csv
echo "$TS,$L,$fp,$go" | tee -a $OUT/ledger.csv
