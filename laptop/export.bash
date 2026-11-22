#!/usr/bin/env bash
# export.bash - write saved scores to a CSV file for Excel / OneDrive.
# One row per saved score, oldest first within each participant.
#
# Usage: ./export.bash [--participant ID] [--db FILE] [--out FILE]
#   Without --participant, every entry is exported.
#   Default output: exports/session_log_<date>_<time>[_P<ID>].csv
set -u

# shellcheck source=common.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.bash"

DB="$DEFAULT_DB"
PARTICIPANT=""
OUT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --participant) PARTICIPANT="${2:-}"; shift 2 ;;
    --db) DB="${2:-}"; shift 2 ;;
    --out) OUT="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

need_sqlite
[ -f "$DB" ] || die "no database at $DB yet. Run a sync first."
init_db "$DB"

WHERE=""
SUFFIX=""
if [ -n "$PARTICIPANT" ]; then
  # Digits only, so it is safe to put in the query.
  [[ "$PARTICIPANT" =~ ^[0-9]{1,5}$ ]] || die "--participant must be a numeric ID"
  WHERE="WHERE e.participant_id = $PARTICIPANT"
  SUFFIX="_P$PARTICIPANT"
fi

if [ -z "$OUT" ]; then
  mkdir -p "$SCRIPT_DIR/exports" || die "cannot create exports folder"
  OUT="$SCRIPT_DIR/exports/session_log_$(date +%Y%m%d_%H%M%S)$SUFFIX.csv"
fi

QUERY="SELECT e.participant_id   AS participant_id,
              p.label            AS participant_label,
              e.exercise_id      AS exercise_id,
              x.name             AS exercise,
              e.technique        AS score,
              e.received_at      AS synced_at,
              e.boot             AS device_boot,
              e.seq              AS device_seq
       FROM entries e
       LEFT JOIN participants p ON p.id = e.participant_id
       LEFT JOIN exercises    x ON x.id = e.exercise_id
       $WHERE
       ORDER BY e.participant_id, e.boot, e.seq;"

sqlite3 -bail -csv -header "$DB" "$QUERY" > "$OUT" || die "export failed"

if [ ! -s "$OUT" ]; then
  # sqlite3 prints no header for an empty result; write one so the file still opens cleanly.
  echo "participant_id,participant_label,exercise_id,exercise,score,synced_at,device_boot,device_seq" > "$OUT"
fi
ROWS=$(( $(wc -l < "$OUT") - 1 ))
echo "Exported $ROWS entr$([ "$ROWS" = 1 ] && echo y || echo ies) to $OUT"
