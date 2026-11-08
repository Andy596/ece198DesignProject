# common.bash - shared helpers for sync.bash and export.bash. Source, don't run.
# Written for bash 3.2 (the version macOS ships), so no associative arrays,
# no mapfile, no fractional read timeouts.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC2034  # used by the scripts that source this file
DEFAULT_DB="$SCRIPT_DIR/session_log.db"

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

need_sqlite() {
  command -v sqlite3 >/dev/null 2>&1 || die "sqlite3 not found. macOS has it built in; on Linux: sudo apt install sqlite3"
}

# init_db <db-path>: create the tables if they don't exist yet.
init_db() {
  sqlite3 -bail "$1" < "$SCRIPT_DIR/schema.sql" || die "could not open or create database $1"
}

# crc8 <text>: prints the CRC-8 (poly 0x07, init 0) of text as two hex digits.
# Must match slog::crc8() in SessionLogStation/session_log.cpp.
crc8() {
  local s="$1" crc=0 i code bit
  for ((i = 0; i < ${#s}; i++)); do
    printf -v code '%d' "'${s:i:1}"
    crc=$(( crc ^ code ))
    for ((bit = 0; bit < 8; bit++)); do
      if (( crc & 0x80 )); then
        crc=$(( ((crc << 1) ^ 0x07) & 0xFF ))
      else
        crc=$(( (crc << 1) & 0xFF ))
      fi
    done
  done
  printf '%02X' "$crc"
}
