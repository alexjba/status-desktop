#!/usr/bin/env bash
# memsnap.sh <label> [pkg]  — snapshot dumpsys meminfo for UI + service process, append CSV row.
set -u
LABEL=${1:-snap}
PKG=${2:-app.status.mobile}
OUT=${OUT:-./mem}
mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M%S)
UI=$(adb shell pidof "$PKG" | tr -d '\r')
GO=$(adb shell pidof "$PKG:statusgo" | tr -d '\r')
row() { # proc pid
  local p=$1 pid=$2 f="$OUT/$TS-$LABEL-$1.txt"
  adb shell dumpsys meminfo "$pid" > "$f"
  local pss rss swap nat gfx apk so unk dalvik
  pss=$(awk '/TOTAL PSS:/{print $3}' "$f"); rss=$(awk '/TOTAL RSS:/{print $6}' "$f"); swap=$(grep -o 'TOTAL SWAP PSS: *[0-9]*' "$f" | awk '{print $4}')
  nat=$(awk '$1=="Native"&&$2=="Heap"{print $3}' "$f"); dalvik=$(awk '$1=="Dalvik"&&$2=="Heap"{print $3}' "$f")
  gfx=$(awk '/EGL mtrack/{print $3}' "$f"); apk=$(awk '/\.apk mmap/{print $3}' "$f"); so=$(awk '/\.so mmap/{print $3}' "$f"); unk=$(awk '$1=="Unknown"{print $2}' "$f")
  st=$(adb shell cat /proc/$pid/status | awk '/VmRSS|VmSwap|Threads/{printf "%s ", $2}')
  echo "$TS,$LABEL,$p,$pid,$pss,$rss,$swap,$nat,$dalvik,$gfx,$apk,$so,$unk,$st" | tee -a "$OUT/ledger.csv"
}
[ -f "$OUT/ledger.csv" ] || echo "ts,label,proc,pid,pss_kb,rss_kb,swappss_kb,native_heap_pss,dalvik_pss,egl_pss,apk_mmap_pss,so_mmap_pss,unknown_pss,vmrss vmswap threads" > "$OUT/ledger.csv"
[ -n "$UI" ] && row ui "$UI"
[ -n "$GO" ] && row go "$GO"
