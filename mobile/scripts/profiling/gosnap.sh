#!/usr/bin/env bash
# gosnap.sh <label>  — snapshot the :statusgo Go runtime through the on-device pprof server.
# Needs a non-release build (pprof auto-starts on 127.0.0.1:6060 inside the service process).
# Writes <OUT>/<ts>-<label>-{memstats.txt,heap.pb.gz,goroutines.txt} and prints the MemStats summary.
set -u
LABEL=${1:-snap}
OUT=${OUT:-./mem}
PORT=${PPROF_PORT:-6060}
mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M%S)
adb forward tcp:$PORT tcp:$PORT >/dev/null
base="$OUT/$TS-$LABEL"
curl -s "http://127.0.0.1:$PORT/debug/pprof/heap?debug=1" > "$base-heap-debug.txt" || { echo "pprof not reachable"; exit 1; }
grep -E '^# (HeapAlloc|HeapSys|HeapIdle|HeapInuse|HeapReleased|StackInuse|StackSys|Sys|Mallocs|Frees|NumGC|NextGC|MSpanInuse|MCacheInuse|GCSys|OtherSys)' "$base-heap-debug.txt" \
  | sed 's/^# //' | tee "$base-memstats.txt"
curl -s "http://127.0.0.1:$PORT/debug/pprof/heap" > "$base-heap.pb.gz"
curl -s "http://127.0.0.1:$PORT/debug/pprof/goroutine?debug=1" | head -1 > "$base-goroutines.txt"
echo "saved $base-*"
