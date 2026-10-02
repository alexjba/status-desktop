#!/usr/bin/env bash
# iossnap.sh <label> — snapshot the simulator app's memory: `footprint` (phys footprint by category, the
# iOS analogue of dumpsys PSS), `vmmap --summary` (malloc zones), and the Go MemStats when the build serves
# pprof on 127.0.0.1:6060 (GO_PPROF=1 build). Appends a CSV row. Simulator apps are host processes.
set -u
LABEL=${1:-snap}
OUT=${OUT:-./mem}
PORT=${PPROF_PORT:-6060}
mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M%S)
PID=${APP_PID:-$(pgrep -f "/Status.app/Status$" | head -1)}
[ -z "$PID" ] && { echo "app not running"; exit 1; }
base="$OUT/$TS-$LABEL"
footprint -p "$PID" > "$base-footprint.txt" 2>&1
vmmap --summary "$PID" > "$base-vmmap.txt" 2>&1
cat_mb() { # category name -> dirty MB
  awk -v c="$1" 'substr($0, length($0)-length(c)+1) == c && index($0, "    " c) {v=$1; u=$2; if(u=="KB") v/=1024; if(u=="GB") v*=1024; if(u=="B") v=0; printf "%.1f", v; found=1; exit} END {if(!found) printf "0"}' "$base-footprint.txt"
}
phys=$(awk '/Footprint:/ {for(i=1;i<=NF;i++) if($i=="Footprint:") {v=$(i+1); u=$(i+2)}} END {if(u=="GB") v*=1024; if(u=="KB") v/=1024; printf "%.1f", v}' "$base-footprint.txt")
rss=$(ps -o rss= -p "$PID" | awk '{printf "%.1f", $1/1024}')
ms=$(cat_mb MALLOC_SMALL); ml=$(cat_mb MALLOC_LARGE); mt=$(cat_mb MALLOC_TINY); mm=$(cat_mb MALLOC_MEDIUM)
ca=$(cat_mb CoreAnimation); cg=$(cat_mb "CG image"); gfx=$(awk '/graphics\)$/ || /IOSurface$/ || /IOAccelerator/ {v=$1; u=$2; if(u=="KB") v/=1024; s+=v} END {printf "%.1f", s}' "$base-footprint.txt")
rosetta=$(awk '/    Rosetta / {v=$1; u=$2; if(u=="KB") v/=1024; s+=v} END {printf "%.1f", s}' "$base-footprint.txt")
nano=$(cat_mb MALLOC_NANO)
data=$(awk '/    __DATA(_DIRTY)?$/ {v=$1; u=$2; if(u=="KB") v/=1024; s+=v} END {printf "%.1f", s}' "$base-footprint.txt")
va=$(cat_mb "untagged (VM_ALLOCATE)"); st=$(cat_mb stack); mf=$(cat_mb "mapped file")
go=""
if curl -s -m 3 "http://127.0.0.1:$PORT/debug/pprof/heap?debug=1" > "$base-heap-debug.txt" 2>/dev/null && [ -s "$base-heap-debug.txt" ]; then
  go=$(grep -E '^# (HeapAlloc|HeapSys|HeapIdle|HeapInuse|HeapReleased|Sys|NumGC) ' "$base-heap-debug.txt" | awk '{printf "%s=%d ", $2, ($2=="NumGC")?$4:$4/1048576}')
  curl -s -m 10 "http://127.0.0.1:$PORT/debug/pprof/heap" > "$base-heap.pb.gz"
fi
[ -f "$OUT/ledger.csv" ] || echo "ts,label,pid,phys_footprint_mb,rss_mb,malloc_tiny,malloc_small,malloc_medium,malloc_large,vm_allocate,coreanimation,cg_image,graphics,data_segments,mapped_file,stack,rosetta,malloc_nano,go_memstats" > "$OUT/ledger.csv"
echo "$TS,$LABEL,$PID,$phys,$rss,$mt,$ms,$mm,$ml,$va,$ca,$cg,$gfx,$data,$mf,$st,$rosetta,$nano,$go" | tee -a "$OUT/ledger.csv"
