#!/usr/bin/env bash
# Startup heapprofd trace across both processes: arm perfetto, relaunch app, log in, settle, pull.
set -u
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
PKG=app.status.mobile.debug
SECS=${SECS:-230}
OUT=$S/mem-profile; mkdir -p $OUT
CFG=$(cat <<EOF
buffers { size_kb: 196608 fill_policy: RING_BUFFER }
data_sources {
  config {
    name: "android.heapprofd"
    heapprofd_config {
      sampling_interval_bytes: 8192
      process_cmdline: "$PKG"
      process_cmdline: "$PKG:statusgo"
      shmem_size_bytes: 8388608
      block_client: true
      all_heaps: true
      continuous_dump_config { dump_phase_ms: 20000 dump_interval_ms: 30000 }
    }
  }
}
duration_ms: $((SECS * 1000))
EOF
)
adb shell setprop persist.traced.enable 1 >/dev/null 2>&1
adb shell am force-stop $PKG
sleep 2
echo "$CFG" | adb shell perfetto --txt -c - -o /data/misc/perfetto-traces/startup.pftrace > $OUT/perfetto.log 2>&1 &
PF=$!
sleep 4
adb shell am start -n $PKG/app.status.mobile.StatusQtActivity >/dev/null 2>&1
sleep 14
adb exec-out screencap -p > $S/h1.png
adb shell input tap 360 935; sleep 0.5; adb shell input text "TestPassword2026"; sleep 0.3; adb shell input keyevent KEYCODE_ENTER
sleep 20
adb exec-out screencap -p > $S/h2.png
OUT=$OUT $S/memsnap.sh hp-postlogin $PKG >/dev/null
sleep 60;  OUT=$OUT $S/memsnap.sh hp-settle-60s $PKG >/dev/null
sleep 60;  OUT=$OUT $S/memsnap.sh hp-settle-120s $PKG >/dev/null
wait $PF
adb pull /data/misc/perfetto-traces/startup.pftrace $OUT/startup.pftrace >/dev/null && ls -la $OUT/startup.pftrace
grep hp- $OUT/ledger.csv | cut -d, -f2-13
