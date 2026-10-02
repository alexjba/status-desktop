#!/usr/bin/env bash
# Device lease for parallel agents. One holder per device, mkdir-atomic.
# usage: lease.sh acquire|release|status|wait <device> [<agent> <issue> [expected-min] [purpose]]
# devices: redmi samsung ipad ios-sim android-emu
set -euo pipefail
ROOT="${LEASE_ROOT:-$HOME/.claude/devices}"
mkdir -p "$ROOT"
cmd="${1:-status}"; dev="${2:-}"; agent="${3:-}"; issue="${4:-}"; mins="${5:-45}"; purpose="${6:-}"

declare -A SERIAL=(
  [redmi]="9b010059305330363400f1f217fa8c"
  [samsung]="adb-RFCRC0FY4ZJ-dSNrUl._adb-tls-connect._tcp"
  [ipad]="00008101-000474143446001E"
  [ios-sim]="simulator"
  [android-emu]="emulator"
)
RULES_redmi="Redmi A5. app.status.mobile.debug holds the OWNER'S profile 'AlexJb': never log into it, never uninstall, never pm clear, install only with 'adb install -r' (check versionCode, add -d only when downgrading on purpose). Create your own test profile (name <agent>-<issue>, password TestPassword2026)."
RULES_samsung="Samsung S21 FE (SM-G990B, wireless adb). Stay-awake enabled by the owner. Run: adb -s \$SERIAL shell svc power stayon true. Same install/profile rules as the Redmi."
RULES_ipad="iPad Air 4 (00008101-000474143446001E). Never touch the profile 'Light Darkslategray Ne…' (owner's). Use the existing test profile or create one named <agent>-<issue>."

lock="$ROOT/$dev.lock"
case "$cmd" in
  acquire)
    [[ -n "$dev" && -n "$agent" && -n "$issue" ]] || { echo "usage: lease.sh acquire <device> <agent> <issue> [min] [purpose]" >&2; exit 2; }
    [[ -n "${SERIAL[$dev]:-}" ]] || { echo "unknown device $dev" >&2; exit 2; }
    if mkdir "$lock" 2>/dev/null; then
      printf '{"agent":"%s","issue":"%s","start":"%s","expected_min":%s,"purpose":"%s","pid":%s}\n' \
        "$agent" "$issue" "$(date -u +%FT%TZ)" "$mins" "$purpose" "$$" > "$lock/holder.json"
      echo "LEASED $dev -> $agent ($issue) for ${mins}min. SERIAL=${SERIAL[$dev]}"
      r="RULES_$dev"; [[ -n "${!r:-}" ]] && echo "RULES: ${!r}"
      [[ "$dev" == samsung ]] && adb -s "${SERIAL[$dev]}" shell svc power stayon true 2>/dev/null || true
    else
      echo "BUSY $dev: $(cat "$lock/holder.json" 2>/dev/null)"; exit 1
    fi ;;
  release)
    [[ -n "$dev" ]] || { echo "usage: lease.sh release <device> <agent>" >&2; exit 2; }
    if [[ -d "$lock" ]]; then
      h=$(cat "$lock/holder.json" 2>/dev/null || echo '{}')
      if [[ -n "$agent" ]] && ! grep -q "\"agent\":\"$agent\"" <<<"$h"; then
        echo "REFUSED: $dev held by someone else: $h" >&2; exit 1
      fi
      rm -rf "$lock"; echo "RELEASED $dev"
    else echo "not leased: $dev"; fi ;;
  wait)
    [[ -n "$dev" && -n "$agent" && -n "$issue" ]] || { echo "usage: lease.sh wait <device> <agent> <issue> [min] [purpose]" >&2; exit 2; }
    deadline=$(( $(date +%s) + ${LEASE_WAIT_MAX:-5400} ))
    while ! "$0" acquire "$dev" "$agent" "$issue" "$mins" "$purpose"; do
      (( $(date +%s) < deadline )) || { echo "TIMEOUT waiting for $dev" >&2; exit 1; }
      sleep 30
    done ;;
  status)
    for d in "${!SERIAL[@]}"; do
      if [[ -d "$ROOT/$d.lock" ]]; then
        h=$(cat "$ROOT/$d.lock/holder.json" 2>/dev/null)
        age=$(( ( $(date +%s) - $(stat -f %m "$ROOT/$d.lock") ) / 60 ))
        echo "$d: BUSY ${age}min $h"
      else echo "$d: free"; fi
    done ;;
  *) echo "unknown command $cmd" >&2; exit 2 ;;
esac
