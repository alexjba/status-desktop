#!/usr/bin/env bash
# measure-run.sh <label> — relaunch the profile build, log in, settle 2 min, then read Go MemStats,
# the idle allocation rate over 60s, and dumpsys. Appends one summary line to bakeoff.csv.
set -u
LABEL=$1
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
PKG=app.status.mobile.debug
OUT=$S/bakeoff; mkdir -p $OUT
export OUT
pkill -f 'run-build.sh' 2>/dev/null; pkill -f 'adb.*logcat' 2>/dev/null
cpu() { # cpu seconds (utime+stime) of a process by name
  local pid; pid=$(adb shell pidof "$1" | tr -d '\r'); [ -z "$pid" ] && { echo 0; return; }
  adb shell cat /proc/$pid/stat | tr -d '\r' | awk '{printf "%.1f", ($14+$15)/100}'
}
adb shell am force-stop $PKG; sleep 2
adb shell am start -n $PKG/app.status.mobile.StatusQtActivity >/dev/null 2>&1
sleep 12
$S/memsnap.sh "$LABEL-login-screen" $PKG >/dev/null
cu0=$(cpu $PKG); cg0=$(cpu $PKG:statusgo)
adb logcat -c
adb shell input tap 360 935; sleep 0.5; adb shell input text "TestPassword2026"; sleep 0.3; adb shell input keyevent KEYCODE_ENTER
sleep 20
cu1=$(cpu $PKG); cg1=$(cpu $PKG:statusgo)
adb logcat -d -v time 2>/dev/null | grep -E 'chronicles|StatusGoService' | grep -v 'callPrivateRPC\|audit' > $OUT/$LABEL-login-logcat.txt
$S/memsnap.sh "$LABEL-postlogin" $PKG >/dev/null
# pprof needs one visibility toggle after the node is up; do it early so the resume storm is over by the settle samples
adb shell input keyevent KEYCODE_HOME; sleep 3
adb shell am start -n $PKG/app.status.mobile.StatusQtActivity >/dev/null 2>&1
sleep 27
cu1=$(cpu $PKG); cg1=$(cpu $PKG:statusgo)
for i in 1 2 3 4; do sleep 30; $S/memsnap.sh "$LABEL-settle-$((i*30))s" $PKG >/dev/null; done
cu2=$(cpu $PKG); cg2=$(cpu $PKG:statusgo)
adb forward tcp:6060 tcp:6060 >/dev/null
curl -s -m 5 'http://127.0.0.1:6060/debug/pprof/heap?debug=1' > $OUT/$LABEL-heapdbg-1.txt
curl -s -m 10 'http://127.0.0.1:6060/debug/pprof/heap' > $OUT/$LABEL-heap.pb.gz
curl -s -m 20 'http://127.0.0.1:6060/debug/pprof/allocs' > $OUT/$LABEL-allocs-A.pb.gz
curl -s -m 45 'http://127.0.0.1:6060/debug/pprof/profile?seconds=30' > $OUT/$LABEL-cpu30.pb.gz
sleep 30
curl -s -m 20 'http://127.0.0.1:6060/debug/pprof/allocs' > $OUT/$LABEL-allocs-B.pb.gz
curl -s -m 5 'http://127.0.0.1:6060/debug/pprof/heap?debug=1' > $OUT/$LABEL-heapdbg-2.txt
$S/memsnap.sh "$LABEL-postpprof" $PKG >/dev/null
curl -s -m 10 'http://127.0.0.1:6060/debug/pprof/heap?gc=1&debug=1' > $OUT/$LABEL-heapdbg-gc.txt
live=$(grep -E '^# HeapAlloc = ' $OUT/$LABEL-heapdbg-gc.txt | awk '{printf "%d", $4/1048576}')
ms() { grep -E "^# $2 = " $OUT/$LABEL-heapdbg-$1.txt | awk '{print $4}'; }
ta1=$(ms 1 TotalAlloc); ta2=$(ms 2 TotalAlloc)
rate=$(( (ta2 - ta1) / 60 / 1024 ))
hs=$(ms 2 HeapSys); hr=$(ms 2 HeapReleased); ha=$(ms 2 HeapAlloc); ngc=$(ms 2 NumGC)
gores=$(( (hs - hr) / 1048576 ))
# settle PSS = mean of the 4 settle samples for the go process; ui likewise
gopss=$(grep -E "^[^,]+,$LABEL-settle-[0-9]+s,go," $OUT/ledger.csv | awk -F, '{s+=$5; n++} END {printf "%d", s/n/1024}')
uipss=$(grep -E "^[^,]+,$LABEL-settle-[0-9]+s,ui," $OUT/ledger.csv | awk -F, '{s+=$5; n++} END {printf "%d", s/n/1024}')
gounk=$(grep -E "^[^,]+,$LABEL-settle-[0-9]+s,go," $OUT/ledger.csv | awk -F, '{s+=$13; n++} END {printf "%d", s/n/1024}')
gonat=$(grep -E "^[^,]+,$LABEL-settle-[0-9]+s,go," $OUT/ledger.csv | awk -F, '{s+=$8; n++} END {printf "%d", s/n/1024}')
d() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.1f", a-b}'; }
gc_login=$(d "$cg1" "$cg0"); ui_login=$(d "$cu1" "$cu0"); gc_idle=$(d "$cg2" "$cg1"); ui_idle=$(d "$cu2" "$cu1")
[ -f $OUT/bakeoff.csv ] || echo "label,go_pss_mb,go_anon_mb,go_native_mb,ui_pss_mb,heapsys_hiwater_mb,go_resident_mb,heapalloc_mb,numgc,idle_alloc_kb_s,go_cpu_login20s,ui_cpu_login20s,go_cpu_idle120s,ui_cpu_idle120s,live_after_gc_mb" > $OUT/bakeoff.csv
echo "$LABEL,$gopss,$gounk,$gonat,$uipss,$((hs/1048576)),$gores,$((ha/1048576)),$ngc,$rate,$gc_login,$ui_login,$gc_idle,$ui_idle,$live" | tee -a $OUT/bakeoff.csv
