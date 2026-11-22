#!/usr/bin/env bash
# participants.bash - manage the participants and exercises the device shows.
# Changes reach the device on the next sync.
#
# Usage:
#   ./participants.bash list
#   ./participants.bash add <id> <label>          e.g. add 104 "Morgan L"
#   ./participants.bash rename <id> <label>
#   ./participants.bash remove <id>               (saved scores are kept)
#   ./participants.bash assign <id> <exercise>    exercise ID or exact name
#   ./participants.bash unassign <id> <exercise>
#   ./participants.bash exercises                 list exercise IDs and names
# Options: --db FILE (before the command)
#
# IDs are 1-65535. Labels are 1-12 characters: letters, digits, space . , ' -
# The device holds up to 10 participants. Use fictional data for demos.
set -u

# shellcheck source=common.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.bash"

DB="$DEFAULT_DB"
MAX_PROFILES=10
RE_LABEL="^[A-Za-z0-9.,'-]([A-Za-z0-9 .,'-]{0,10}[A-Za-z0-9.,'-])?\$"

usage() { sed -n '2,17p' "$0"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --db) DB="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) break ;;
  esac
done
[ $# -gt 0 ] || { usage; exit 1; }

need_sqlite
init_db "$DB"

sql() { sqlite3 -bail "$DB" "$@" </dev/null; }
# Doubles single quotes so text can go inside '...' in SQL.
quote() { printf "'%s'" "${1//\'/\'\'}"; }

check_id() {
  [[ "$1" =~ ^[0-9]{1,5}$ ]] && [ "$1" -ge 1 ] && [ "$1" -le 65535 ] ||
    die "participant ID must be a number from 1 to 65535 (got '$1')"
}
check_label() {
  [[ "$1" =~ $RE_LABEL ]] ||
    die "label must be 1-12 characters: letters, digits, space . , ' - and no leading/trailing space (got '$1')"
}
exists() { [ "$(sql "SELECT COUNT(*) FROM participants WHERE id = $1;")" = 1 ]; }

# Prints the exercise ID for an ID or an exact (case-insensitive) name.
exercise_id() {
  local id
  if [[ "$1" =~ ^[0-9]{1,2}$ ]]; then
    id="$(sql "SELECT id FROM exercises WHERE id = $1;")"
  else
    id="$(sql "SELECT id FROM exercises WHERE lower(name) = lower($(quote "$1"));")"
  fi
  [ -n "$id" ] || die "unknown exercise '$1' (see: ./participants.bash exercises)"
  printf '%s' "$id"
}

cmd="$1"
shift
case "$cmd" in
  list)
    [ $# -eq 0 ] || die "usage: list"
    sql -header -column "SELECT p.id AS id, p.label AS label,
        COALESCE((SELECT group_concat(name, ', ') FROM
          (SELECT x.name FROM assignments a JOIN exercises x ON x.id = a.exercise_id
           WHERE a.participant_id = p.id ORDER BY x.id)), '') AS assigned
      FROM participants p ORDER BY p.id;"
    ;;
  add)
    [ $# -eq 2 ] || die "usage: add <id> <label>"
    check_id "$1"
    check_label "$2"
    exists "$1" && die "participant $1 already exists (use rename)"
    count="$(sql "SELECT COUNT(*) FROM participants;")"
    [ "$count" -lt "$MAX_PROFILES" ] || die "the device holds at most $MAX_PROFILES participants; remove one first"
    sql "INSERT INTO participants (id, label) VALUES ($1, $(quote "$2"));"
    echo "Added participant $1 ($2). Sync the device to load it."
    ;;
  rename)
    [ $# -eq 2 ] || die "usage: rename <id> <label>"
    check_id "$1"
    check_label "$2"
    exists "$1" || die "no participant $1"
    sql "UPDATE participants SET label = $(quote "$2") WHERE id = $1;"
    echo "Renamed participant $1 to $2."
    ;;
  remove)
    [ $# -eq 1 ] || die "usage: remove <id>"
    check_id "$1"
    exists "$1" || die "no participant $1"
    sql "BEGIN; DELETE FROM assignments WHERE participant_id = $1;
         DELETE FROM participants WHERE id = $1; COMMIT;"
    echo "Removed participant $1. Their saved scores stay in the database."
    ;;
  assign|unassign)
    [ $# -eq 2 ] || die "usage: $cmd <id> <exercise>"
    check_id "$1"
    exists "$1" || die "no participant $1"
    ex="$(exercise_id "$2")" || exit 1  # die in $(...) only leaves the subshell
    name="$(sql "SELECT name FROM exercises WHERE id = $ex;")"
    if [ "$cmd" = assign ]; then
      sql "INSERT OR IGNORE INTO assignments (participant_id, exercise_id) VALUES ($1, $ex);"
      echo "Assigned $name to participant $1."
    else
      sql "DELETE FROM assignments WHERE participant_id = $1 AND exercise_id = $ex;"
      echo "Unassigned $name from participant $1 (exercises with saved scores still show)."
    fi
    ;;
  exercises)
    [ $# -eq 0 ] || die "usage: exercises"
    sql -header -column "SELECT id, name FROM exercises ORDER BY id;"
    ;;
  *)
    die "unknown command '$cmd' (try --help)"
    ;;
esac
