#!/usr/bin/env bash
# sync.bash - copy saved scores from the Session Log Station into SQLite, then
# load the current participant list onto the device.
#
# Usage: ./sync.bash [--port DEVICE] [--db FILE] [--timeout SECONDS]
#
# The device is only told the entries are safe (ACK) after the database
# commit succeeds. Running this twice never creates duplicates.
# Exit codes: 0 ok, 1 transfer failed (nothing marked synced), 2 saved but the
# device did not confirm (run again before switching off), 3 saved, but the
# participant list could not be loaded onto the device (run again).
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
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
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

# --- Participant list (laptop -> device) -------------------------------------
# Must match pstore limits in ece198Codes1.0.2/profile_store.h.
MAX_PROFILES=10
EXERCISE_COUNT=55
RE_LABEL="^[A-Za-z0-9.,'-]([A-Za-z0-9 .,'-]{0,10}[A-Za-z0-9.,'-])?\$"
# Same rules in SQL, so bad rows never reach the line parser below.
VALID_LABEL_SQL="length(label) BETWEEN 1 AND 12 AND label = trim(label) AND label NOT GLOB '*[^A-Za-z0-9 .,''-]*'"
PUSH_ERROR=""

expect_ok() {  # expect_ok <what>: the device must answer OK
  if ! recv; then
    PUSH_ERROR="no answer from device while sending $1"
    return 1
  fi
  if [ "$PAYLOAD" != "OK" ]; then
    PUSH_ERROR="device refused $1 ($PAYLOAD)"
    return 1
  fi
}

# Prints "id|label" for every participant the device can show, lowest ID first.
valid_participants() {
  sqlite3 -bail "$DB" "SELECT id, label FROM participants
    WHERE id BETWEEN 1 AND 65535 AND $VALID_LABEL_SQL ORDER BY id;" </dev/null
}

# Prints "exercise|score" for one participant: every assigned exercise plus
# every exercise that has a saved score. score is the latest saved score, or -1.
participant_exercises() {  # participant_exercises <id>
  sqlite3 -bail "$DB" "WITH ex AS (
      SELECT exercise_id AS eid FROM assignments WHERE participant_id = $1
      UNION
      SELECT exercise_id FROM entries WHERE participant_id = $1)
    SELECT eid, COALESCE((SELECT technique FROM entries e
                          WHERE e.participant_id = $1 AND e.exercise_id = ex.eid
                            AND e.technique BETWEEN 0 AND 100
                          ORDER BY e.boot DESC, e.seq DESC LIMIT 1), -1)
    FROM ex WHERE eid BETWEEN 0 AND $((EXERCISE_COUNT - 1)) ORDER BY eid;" </dev/null
}

# push_profiles: returns 0 on success, 1 on failure (PUSH_ERROR says why),
# 3 if the device firmware has no profile support.
push_profiles() {
  local rows total skipped pid label eid score ex_rows n=0 a=0
  rows="$(valid_participants)" || { PUSH_ERROR="could not read participants"; return 1; }
  total="$(sqlite3 -bail "$DB" "SELECT COUNT(*) FROM participants;" </dev/null)" || total=0
  skipped="$(sqlite3 -bail "$DB" "SELECT id || ' (' || label || ')' FROM participants
    WHERE NOT (id BETWEEN 1 AND 65535 AND $VALID_LABEL_SQL) ORDER BY id;" </dev/null)" || skipped=""
  if [ -n "$skipped" ]; then
    echo "WARNING: these participants can't be shown on the device and were skipped" >&2
    echo "  (labels must be 1-12 characters: letters, digits, space . , ' -):" >&2
    printf '%s\n' "$skipped" | sed 's/^/    /' >&2
  fi
  [ -n "$rows" ] && n="$(printf '%s\n' "$rows" | wc -l | tr -d ' ')"
  if [ "$n" -gt "$MAX_PROFILES" ]; then
    echo "WARNING: the device holds $MAX_PROFILES participants; only the $MAX_PROFILES lowest IDs were loaded (of $n)." >&2
    rows="$(printf '%s\n' "$rows" | head -n "$MAX_PROFILES")"
    n=$MAX_PROFILES
  fi

  send "PROFILES $n"
  if ! recv; then
    PUSH_ERROR="no answer from device"
    return 1
  fi
  case "$PAYLOAD" in
    OK) ;;
    "ERR unknown_command") return 3 ;;
    *) PUSH_ERROR="device refused the list ($PAYLOAD)"; return 1 ;;
  esac

  while IFS='|' read -r pid label; do
    [ -n "$pid" ] || continue
    # Re-check what SQL already filtered: this text goes onto the wire.
    [[ "$pid" =~ ^[0-9]{1,5}$ && "$label" =~ $RE_LABEL ]] || { PUSH_ERROR="bad participant row: $pid"; return 1; }
    send "P $pid $label"
    expect_ok "participant $pid" || return 1
    ex_rows="$(participant_exercises "$pid")" || { PUSH_ERROR="could not read exercises for $pid"; return 1; }
    while IFS='|' read -r eid score; do
      [ -n "$eid" ] || continue
      [[ "$eid" =~ ^[0-9]{1,2}$ && "$score" =~ ^-?[0-9]{1,3}$ ]] || { PUSH_ERROR="bad exercise row for $pid"; return 1; }
      send "A $pid $eid $score"
      expect_ok "exercise $eid for participant $pid" || return 1
      a=$((a + 1))
    done <<< "$ex_rows"
  done <<< "$rows"

  send "PEND $n $a"
  PAYLOAD=""
  if ! recv || [ "$PAYLOAD" != "PDONE $n $a" ]; then
    PUSH_ERROR="device did not confirm the list (${PAYLOAD:-no answer})"
    return 1
  fi
  echo "Loaded $n participant$([ "$n" = 1 ] || echo s) onto the device ($a exercises)."
  [ "$total" -gt 0 ] || echo "No participants in the database yet. Add some with ./participants.bash add."
  return 0
}

# finish <remaining>: load the participant list, then report.
finish() {
  local remaining="$1" rc=0
  push_profiles || rc=$?
  if [ "$rc" -eq 3 ]; then
    echo "Note: this device's sketch doesn't take participant lists (it isn't 1.0.2 or newer)."
    rc=0
  elif [ "$rc" -ne 0 ]; then
    echo "WARNING: scores are saved on this laptop, but the participant list was not loaded: $PUSH_ERROR" >&2
    echo "Run sync again." >&2
    exit 3
  fi
  if [ "$remaining" -eq 0 ]; then
    echo "Safe to switch off."
  else
    echo "$remaining new entr$([ "$remaining" = 1 ] && echo y || echo ies) were saved on the device during the sync. Run sync again."
  fi
  exit 0
}

if [ "$COUNT" -eq 0 ]; then
  echo "Nothing new on the device."
  finish 0
fi

# Commit first, ACK second. If the commit fails we send nothing, so the device
# keeps the entries.
NEW="$(sqlite3 -bail "$DB" < "$SQL_FILE")" || fail "could not write to the database"
echo "Saved $NEW new entr$([ "$NEW" = 1 ] && echo y || echo ies) ($((COUNT - NEW)) were already in the database)."

send "ACK $BOOT $last_seq"
RE_DONE="^DONE $BOOT $last_seq ([0-9]{1,5})\$"
if recv && [[ "$PAYLOAD" =~ $RE_DONE ]]; then
  echo "Device confirmed."
  finish "${BASH_REMATCH[1]}"
fi

echo "WARNING: entries are saved on this laptop, but the device did not confirm." >&2
echo "Run sync again before switching the device off (no duplicates will be created)." >&2
exit 2
