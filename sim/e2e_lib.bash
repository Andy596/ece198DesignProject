# e2e_lib.bash - helpers shared by the end-to-end tests. Source, don't run.
# Expects HERE (this folder) to be set by the caller.

LAPTOP="$HERE/../laptop"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/slog_e2e.XXXXXX")"
DB="$WORK/test.db"
EEPROM="$WORK/eeprom.bin"
SIM_BIN="${SIM_BIN:-$HERE/sim_device}"
PASS=0
FAIL=0
SIM_PID=""

cleanup() {
  stop_sim
  rm -rf "$WORK"
}
trap cleanup EXIT

command -v sqlite3 >/dev/null 2>&1 || { echo "sqlite3 is required for these tests (sudo apt install sqlite3)" >&2; exit 1; }

check() {  # check <description> <command...>
  local desc="$1"
  shift
  if "$@"; then
    PASS=$((PASS + 1))
    echo "  ok   $desc"
  else
    FAIL=$((FAIL + 1))
    echo "  FAIL $desc"
  fi
}

start_sim() {  # start_sim [sim options]  -> sets PORT, control on fd 4
  rm -f "$WORK/port" "$WORK/ctl"
  mkfifo "$WORK/ctl"
  "$SIM_BIN" --ready-file "$WORK/port" --eeprom "$EEPROM" "$@" \
    < "$WORK/ctl" > "$WORK/sim.out" 2> "$WORK/sim.lcd" &
  SIM_PID=$!
  exec 4> "$WORK/ctl"
  for _ in $(seq 100); do
    [ -s "$WORK/port" ] && break
    sleep 0.05
  done
  PORT="$(cat "$WORK/port")"
}

stop_sim() {
  if [ -n "$SIM_PID" ]; then
    echo quit >&4 2>/dev/null
    exec 4>&-
    wait "$SIM_PID" 2>/dev/null
    SIM_PID=""
  fi
}

sim_cmd() {  # sim_cmd <command>  -> prints the simulator's reply line
  local before
  before=$(wc -l < "$WORK/sim.out")
  echo "$1" >&4
  for _ in $(seq 250); do
    [ "$(wc -l < "$WORK/sim.out")" -gt "$before" ] && break
    sleep 0.02
  done
  tail -n 1 "$WORK/sim.out"
}

run_sync() {  # run_sync [extra args] -> sets RC and OUT
  OUT="$("$LAPTOP/sync.bash" --port "$PORT" --db "$DB" --timeout 2 "$@" 2>&1)"
  RC=$?
}

db_count() { sqlite3 "$DB" "SELECT COUNT(*) FROM entries;"; }
has() { [[ "$1" == *"$2"* ]]; }
eq() { [ "$1" = "$2" ]; }

finish_tests() {
  echo
  echo "$PASS passed, $FAIL failed"
  [ "$FAIL" -eq 0 ]
}
