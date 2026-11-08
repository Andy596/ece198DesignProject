#!/usr/bin/env bash
# sync.bash - copy unsynced entries from the Session Log Station into SQLite.
#
# Usage: ./sync.bash [--port DEVICE] [--db FILE] [--timeout SECONDS]
#
# The device is only told the entries are safe (ACK) after the database
# commit succeeds. Running this twice never creates duplicates.
# Exit codes: 0 ok, 1 transfer failed (nothing marked synced), 2 saved but the
# device did not confirm (run again before switching off).
set -u

# shellcheck source=common.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.bash"

PORT=""
DB="$DEFAULT_DB"
TIMEOUT=5

while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT="${2:-}"; shift 2 ;;
    --db) DB="${2:-}"; shift 2 ;;
    --timeout) TIMEOUT="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done
[[ "$TIMEOUT" =~ ^[1-9][0-9]?$ ]] || die "--timeout must be a whole number of seconds (1-99)"

need_sqlite
init_db "$DB"

# --- Find the board --------------------------------------------------------
if [ -z "$PORT" ]; then
  found=()
  # macOS uses /dev/cu.* (not /dev/tty.*, which waits for carrier); Linux uses ttyACM.
  for p in /dev/cu.usbmodem* /dev/ttyACM*; do
    [ -e "$p" ] && found+=("$p")
  done
  if [ "${#found[@]}" -eq 0 ]; then
    die "no device found. Plug the Session Log Station in by USB (or pass --port)."
  elif [ "${#found[@]}" -gt 1 ]; then
    die "several serial devices found: ${found[*]}. Pick one with --port."
  fi
  PORT="${found[0]}"
fi
echo "Device: $PORT"
echo "Database: $DB"

# Keep the port open on fd 3 for the whole session. Configuring it through
# stdin works with both macOS (BSD) and Linux (GNU) stty, and the settings
# stay in effect because the descriptor stays open.
exec 3<>"$PORT" || die "cannot open $PORT (on Linux, add yourself to the dialout group)"
stty raw -echo 115200 <&3 2>/dev/null || stty raw -echo <&3 || die "cannot configure $PORT"

SQL_FILE="$(mktemp "${TMPDIR:-/tmp}/slog_sync.XXXXXX")" || die "cannot create temp file"
trap 'rm -f "$SQL_FILE"; exec 3>&-' EXIT

# --- Protocol helpers --------------------------------------------------------
send() {  # send <payload>
  printf '%s*%s\n' "$1" "$(crc8 "$1")" >&3
}

# recv: read one CRC-checked line into PAYLOAD. Returns 1 on timeout, 2 on a
# bad line. Lines that aren't protocol lines (debug prints) are skipped.
PAYLOAD=""
RE_FRAMED='^(.*)\*([0-9A-F]{2})$'
recv() {
  local line skipped=0
  while :; do
    IFS= read -r -t "$TIMEOUT" line <&3 || return 1
    line="${line%$'\r'}"
    if [[ "$line" =~ $RE_FRAMED ]]; then
      PAYLOAD="${BASH_REMATCH[1]}"
      if [ "$(crc8 "$PAYLOAD")" != "${BASH_REMATCH[2]}" ]; then
        echo "  corrupted line: $line" >&2
        return 2
      fi
      return 0
    fi
    skipped=$((skipped + 1))
    [ "$skipped" -lt 50 ] || return 2
  done
}

fail() {
  echo "TRANSFER FAILED: $*" >&2
  echo "Nothing was marked as synced. The entries are still on the device; plug in and run sync again." >&2
  exit 1
}

# --- Transfer ------------------------------------------------------------------
send "SYNC"

# Skip any stale lines left over from an earlier session until BEGIN arrives.
RE_BEGIN='^BEGIN ([0-9]{1,10}) ([0-9]{1,5})$'
for ((tries = 0; ; tries++)); do
  (( tries < 20 )) || fail "device never started the transfer"
  recv || fail "no reply from device (is the Session Log Station sketch running?)"
  [[ "$PAYLOAD" =~ $RE_BEGIN ]] && break
done
BOOT="${BASH_REMATCH[1]}"
COUNT="${BASH_REMATCH[2]}"

RE_ENTRY='^E ([0-9]{1,10}) ([0-9]{1,5}) ([0-9]{1,5}) ([0-9]{1,3}) (-?[0-9]{1,5}) ([0-9]{1,3})$'
{
  echo "BEGIN;"
  last_seq=-1
  for ((i = 0; i < COUNT; i++)); do
    recv || fail "lost connection or corrupted data at entry $((i + 1)) of $COUNT"
    [[ "$PAYLOAD" =~ $RE_ENTRY ]] || fail "malformed entry: $PAYLOAD"
    [ "${BASH_REMATCH[1]}" = "$BOOT" ] || fail "entry from a different boot: $PAYLOAD"
    seq="${BASH_REMATCH[2]}"
    (( seq > last_seq )) || fail "entries out of order at seq $seq"
    last_seq=$seq
    # Every field was checked as digits by the regex, so this SQL is safe.
    printf "INSERT OR IGNORE INTO entries (boot, seq, participant_id, exercise_id, value, technique, received_at) VALUES (%s, %s, %s, %s, %s, %s, strftime('%%Y-%%m-%%d %%H:%%M:%%S', 'now', 'localtime'));\n" \
      "$BOOT" "$seq" "${BASH_REMATCH[3]}" "${BASH_REMATCH[4]}" "${BASH_REMATCH[5]}" "${BASH_REMATCH[6]}"
  done
  echo "SELECT total_changes();"
  echo "COMMIT;"
} > "$SQL_FILE"

recv || fail "no END marker from device"
[ "$PAYLOAD" = "END $BOOT $COUNT" ] || fail "unexpected END: $PAYLOAD"

if [ "$COUNT" -eq 0 ]; then
  echo "Nothing new on the device. Safe to switch off."
  exit 0
fi

# Commit first, ACK second. If the commit fails we send nothing, so the device
# keeps the entries.
NEW="$(sqlite3 -bail "$DB" < "$SQL_FILE")" || fail "could not write to the database"
echo "Saved $NEW new entr$([ "$NEW" = 1 ] && echo y || echo ies) ($((COUNT - NEW)) were already in the database)."

send "ACK $BOOT $last_seq"
RE_DONE="^DONE $BOOT $last_seq ([0-9]{1,5})\$"
if recv && [[ "$PAYLOAD" =~ $RE_DONE ]]; then
  remaining="${BASH_REMATCH[1]}"
  if [ "$remaining" -eq 0 ]; then
    echo "Device confirmed. Safe to switch off."
  else
    echo "Device confirmed, but $remaining entr$([ "$remaining" = 1 ] && echo y || echo ies) were added during the sync. Run sync again."
  fi
  exit 0
fi

echo "WARNING: entries are saved on this laptop, but the device did not confirm." >&2
echo "Run sync again before switching the device off (no duplicates will be created)." >&2
exit 2
