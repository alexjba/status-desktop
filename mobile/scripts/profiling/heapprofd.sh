#!/usr/bin/env bash
# heapprofd.sh <label> [pkg] [seconds] — scripted native-heap (malloc) profile of a profileable/debuggable
# process via Perfetto heapprofd. Same data Android Studio's "Native allocations" recording shows.
# Output: <OUT>/<ts>-<label>.pftrace  (open in https://ui.perfetto.dev or query with trace_processor).
set -u
LABEL=${1:-heap}
PKG=${2:-app.status.mobile.debug}
SECS=${3:-30}
OUT=${OUT:-./mem}
mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M%S)
CFG=$(cat <<EOF
buffers { size_kb: 262144 fill_policy: RING_BUFFER }
data_sources {
  config {
    name: "android.heapprofd"
    heapprofd_config {
      sampling_interval_bytes: 4096
      process_cmdline: "$PKG"
      shmem_size_bytes: 8388608
      block_client: true
      all_heaps: true
      continuous_dump_config { dump_phase_ms: 5000 dump_interval_ms: 10000 }
    }
  }
}
duration_ms: $((SECS * 1000))
EOF
)
adb shell setprop persist.traced.enable 1 >/dev/null 2>&1
echo "$CFG" | adb shell perfetto --txt -c - -o /data/misc/perfetto-traces/$LABEL.pftrace
adb pull /data/misc/perfetto-traces/$LABEL.pftrace "$OUT/$TS-$LABEL.pftrace" >/dev/null && echo "saved $OUT/$TS-$LABEL.pftrace"
