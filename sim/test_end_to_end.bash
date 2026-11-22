#!/usr/bin/env bash
# End-to-end tests for the original SessionLogStation transfer demo (in
# sim_device) <-> laptop/sync.bash and export.bash over a pseudo-terminal.
# The 1.0.2 sketch has its own suite: test_ui_end_to_end.bash.
# Run from sim/:  make test
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=e2e_lib.bash
source "$HERE/e2e_lib.bash"

echo "1. First sync of the 3 demo entries"
start_sim
run_sync
check "sync exits 0" eq "$RC" 0
check "reports 3 new entries" has "$OUT" "Saved 3 new entries"
check "device confirms, safe to switch off" has "$OUT" "Safe to switch off"
check "old sketch: participant list skipped with a note" has "$OUT" "doesn't take participant lists"
check "database has 3 rows" eq "$(db_count)" 3
check "row matches device data (P101 squat 12 reps, technique 86)" \
  eq "$(sqlite3 "$DB" "SELECT participant_id,exercise_id,value,technique FROM entries WHERE seq=0;")" "101|0|12|86"
check "device reports nothing unsynced" has "$(sim_cmd status)" "unsynced=0 safe=1"
check "LCD showed 'Sync complete / 3 saved'" grep -q "Sync complete   |3 saved" "$WORK/sim.lcd"

echo "2. Syncing again with nothing new"
run_sync
check "sync exits 0" eq "$RC" 0
check "says nothing new" has "$OUT" "Nothing new"
check "still 3 rows (no duplicates)" eq "$(db_count)" 3

echo "3. New entries through the confirmEntry() hook"
check "add entry accepted" eq "$(sim_cmd 'add 104 3 45 70')" "ADD ok"
check "add entry with negative value accepted" eq "$(sim_cmd 'add 101 4 -2 100')" "ADD ok"
check "technique above max rejected" eq "$(sim_cmd 'add 101 0 1 101')" "ADD rejected"
check "unknown exercise rejected" eq "$(sim_cmd 'add 101 9 1 50')" "ADD rejected"
check "undo removes the newest unsynced entry" eq "$(sim_cmd undo)" "UNDO ok"
check "add again" eq "$(sim_cmd 'add 101 4 -2 100')" "ADD ok"
check "device has 2 unsynced" has "$(sim_cmd status)" "unsynced=2"
run_sync
check "sync exits 0" eq "$RC" 0
check "database has 5 rows" eq "$(db_count)" 5
check "negative value stored" eq "$(sqlite3 "$DB" "SELECT value FROM entries WHERE participant_id=101 AND exercise_id=4;")" "-2"
check "undo after sync is refused" eq "$(sim_cmd undo)" "UNDO rejected"
stop_sim

echo "4. ACK lost on the way to the device"
start_sim --drop-ack-once
check "boot counter advanced after power cycle" has "$(sim_cmd status)" "boot=1"
run_sync
check "sync exits 2 (saved but unconfirmed)" eq "$RC" 2
check "warns to sync again" has "$OUT" "Run sync again before switching the device off"
check "entries are in the database" eq "$(db_count)" 8
check "device still lists them as unsynced" has "$(sim_cmd status)" "unsynced=3 safe=0"
run_sync
check "retry exits 0" eq "$RC" 0
check "retry finds them already saved" has "$OUT" "Saved 0 new entries (3 were already in the database)"
check "no duplicates after retry" eq "$(db_count)" 8
check "device now safe to switch off" has "$(sim_cmd status)" "unsynced=0 safe=1"
stop_sim

echo "5. Corrupted line during transfer"
start_sim --corrupt-entry-once
run_sync
check "sync exits 1" eq "$RC" 1
check "reports transfer failed" has "$OUT" "TRANSFER FAILED"
check "nothing written to the database" eq "$(db_count)" 8
check "device keeps the entries" has "$(sim_cmd status)" "unsynced=3"
# The device is still waiting for an ACK that will never come; a fresh SYNC
# simply resends everything.
run_sync
check "retry succeeds" eq "$RC" 0
check "3 new rows from boot 2" eq "$(sqlite3 "$DB" "SELECT COUNT(*) FROM entries WHERE boot=2;")" 3
stop_sim

echo "6. Device not answering"
start_sim --mute
run_sync --timeout 1
check "sync exits 1" eq "$RC" 1
check "explains there was no reply" has "$OUT" "no reply from device"
stop_sim

echo "7. ACK timeout shown on the device"
start_sim --drop-ack-once
run_sync
sleep 11
check "LCD showed 'Sync failed'" grep -q "Sync failed" "$WORK/sim.lcd"
run_sync
check "recovers after timeout" eq "$RC" 0
stop_sim

echo "8. Bad options"
OUT="$("$LAPTOP/sync.bash" --port /dev/does-not-exist --db "$DB" 2>&1)"; RC=$?
check "missing port fails cleanly" eq "$RC" 1
check "bad --timeout rejected" bash -c "! '$LAPTOP/sync.bash' --timeout abc --db '$DB' >/dev/null 2>&1"

echo "9. CSV export"
sqlite3 "$DB" "INSERT OR REPLACE INTO participants (id, label) VALUES (101, 'Demo, \"A\"'), (102, 'Demo B');"
OUT="$("$LAPTOP/export.bash" --db "$DB" --out "$WORK/all.csv")"; RC=$?
TOTAL="$(db_count)"
check "export exits 0" eq "$RC" 0
check "one CSV row per database row" eq "$(( $(wc -l < "$WORK/all.csv") - 1 ))" "$TOTAL"
check "header row" eq "$(head -n 1 "$WORK/all.csv")" \
  "participant_id,participant_label,exercise_id,exercise,score,synced_at,device_boot,device_seq"
check "label with comma and quotes is escaped" grep -q '"Demo, ""A"""' "$WORK/all.csv"
check "exercise names joined in" grep -q ",0,Arm rainbows,86," "$WORK/all.csv"
# Round-trip: parse the CSV back with sqlite and compare to the table.
sqlite3 "$WORK/check.db" ".import --csv '$WORK/all.csv' t"
check "CSV values match the database exactly" eq \
  "$(sqlite3 "$WORK/check.db" "SELECT participant_id,exercise_id,score,device_boot,device_seq FROM t ORDER BY device_boot,device_seq;")" \
  "$(sqlite3 "$DB" "SELECT participant_id,exercise_id,technique,boot,seq FROM entries ORDER BY boot,seq;")"
"$LAPTOP/export.bash" --db "$DB" --participant 101 --out "$WORK/p101.csv" >/dev/null
check "participant filter keeps only P101" \
  eq "$(tail -n +2 "$WORK/p101.csv" | cut -d, -f1 | sort -u)" "101"
"$LAPTOP/export.bash" --db "$DB" --participant 999 --out "$WORK/none.csv" >/dev/null
check "empty export still has a header" eq "$(wc -l < "$WORK/none.csv" | tr -d ' ')" 1
check "non-numeric participant rejected" bash -c "! '$LAPTOP/export.bash' --db '$DB' --participant '1 OR 1=1' >/dev/null 2>&1"

finish_tests
