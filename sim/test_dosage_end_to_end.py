#!/usr/bin/env python3
"""End-to-end tests: the real 1.0.8 sketch (sim_dosage_device) <-> the real
laptop/sync_dosages.py receiver over a pseudo-terminal, into SQLite.

The device is driven through its buttons, knob and A3 touch sensor exactly as
an instructor would. Run from sim/:  make test   (or python3 test_dosage_end_to_end.py)
"""

import csv
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
LAPTOP = HERE.parent / "laptop"
RECEIVER = LAPTOP / "sync_dosages.py"
EXPORTER = LAPTOP / "export_dosages.py"

PASS = 0
FAIL = 0


def check(desc, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  ok   {desc}")
    else:
        FAIL += 1
        print(f"  FAIL {desc}" + (f"\n       {detail}" if detail else ""))


class Device:
    """One power-up of the simulated board. EEPROM persists in a file."""

    def __init__(self, work, *options, binary="sim_dosage_device"):
        self.work = work
        port_file = work / "port"
        port_file.unlink(missing_ok=True)
        self.lcd_log = open(work / "lcd.log", "a")
        self.proc = subprocess.Popen(
            [str(HERE / binary), "--ready-file", str(port_file), "--eeprom", str(work / "eeprom.bin"), *options],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.lcd_log, text=True, bufsize=1)
        for _ in range(200):
            if port_file.exists() and port_file.read_text().strip():
                break
            time.sleep(0.02)
        self.port = port_file.read_text().strip()

    def cmd(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        reply = self.proc.stdout.readline().strip()
        if not reply:
            raise RuntimeError(f"simulator stopped (command {line!r})")
        return reply

    def screen(self):
        m = re.match(r"SCREEN (\d+) LCD \|(.*?)\|(.*?)\| OLED (.*)", self.cmd("screen"))
        return int(m.group(1)), m.group(2).rstrip(), m.group(3).rstrip(), m.group(4)

    def lcd(self):
        _, l0, l1, _ = self.screen()
        return l0, l1

    def entries(self):
        return self.cmd("entries")

    def identity(self):
        _, device_id, boot = self.cmd("identity").split()
        return device_id, int(boot)

    def stop(self):
        try:
            self.cmd("quit")
        except (RuntimeError, BrokenPipeError):
            pass
        self.proc.wait(timeout=5)
        self.lcd_log.close()

    # --- Instructor actions --------------------------------------------------

    def open_user(self, name):
        """Opens a participant's profile from wherever the device is."""
        self.to_main_menu()
        self.cmd("scroll -12")  # top of the list
        for _ in range(12):
            l0, _ = self.lcd()
            if l0 == name:
                self.cmd("enter")
                return
            self.cmd("scroll 1")
        raise RuntimeError(f"no participant {name}")

    def to_main_menu(self):
        for _ in range(6):
            if self.screen()[0] == 0:
                return
            self.cmd("back")
        raise RuntimeError("could not return to the main menu")

    def new_session(self, name):
        self.open_user(name)
        self.select_row("New session")
        self.cmd("enter")
        self.to_main_menu()

    def select_row(self, label):
        """Scrolls the current OLED menu until `label` is highlighted."""
        self.cmd("scroll -60")
        for _ in range(60):
            if f"> {label}" in self.screen()[3]:
                return
            self.cmd("scroll 1")
        raise RuntimeError(f"no row {label}")

    def set_number(self, target, parse):
        for _ in range(4):
            current = parse(self.lcd()[1])
            if current == target:
                return
            self.cmd(f"scroll {target - current}")
        raise RuntimeError(f"could not set {target} (LCD {self.lcd()})")

    def set_choice(self, want, left, right):
        self.cmd("scroll -1" if want == left else "scroll 1")
        got = self.lcd()[1]
        if got != want:
            raise RuntimeError(f"wanted {want}, LCD shows {got}")

    def log_completed(self, name, exercise, sets, amount, measure, load_quarters, unit):
        """Main menu -> participant -> exercise -> Log completed -> save -> main menu."""
        self.open_user(name)
        self.select_row(exercise)
        self.cmd("enter")
        self.select_row("Log completed")
        self.cmd("enter")
        self.set_number(sets, int)                                        # Sets
        self.cmd("enter")
        self.set_choice("Repetitions" if measure == "reps" else "Seconds", "Repetitions", "Seconds")
        self.cmd("enter")
        self.set_number(amount, int)                                      # Reps or seconds per set
        self.cmd("enter")
        self.set_choice("Kilograms" if unit == "kg" else "Pounds", "Kilograms", "Pounds")
        self.cmd("enter")
        self.set_number(load_quarters, parse_load)                        # Load per set
        self.cmd("enter")
        l0, l1 = self.lcd()
        if not l0.startswith(f"{sets} x {amount} "):
            raise RuntimeError(f"review screen shows {l0!r} {l1!r}")
        self.cmd("enter")                                                 # save
        self.to_main_menu()


def parse_load(text):
    whole, _, frac = text.split()[0].partition(".")
    return int(whole) * 4 + {"": 0, "25": 1, "5": 2, "75": 3}[frac]


class Receiver:
    def __init__(self, port, db, wait=8, port_override=None):
        self.proc = subprocess.Popen(
            [sys.executable, str(RECEIVER), "--port", port_override or port, "--db", str(db), "--wait", str(wait)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.first = ""
        while True:  # warnings may come before "Listening ..."
            line = self.proc.stdout.readline()
            self.first += line
            if not line or line.startswith("Listening"):
                break

    def finish(self, timeout=40):
        rest, _ = self.proc.communicate(timeout=timeout)
        return self.proc.returncode, self.first + rest


def sync(dev, db, tap=True, wait=8):
    """Runs the receiver, taps A3 on the main menu, and returns (rc, output, lcd)."""
    rx = Receiver(dev.port, db, wait=wait)
    if "Listening" not in rx.first:
        rc, out = rx.finish()
        return rc, out, dev.lcd()
    if tap:
        dev.to_main_menu()  # A3 starts a sync only from the main menu
        dev.cmd("touch")
    rc, out = rx.finish()
    time.sleep(0.1)
    dev.cmd("wait 20")
    return rc, out, dev.lcd()


def entries_in_db(db):
    con = sqlite3.connect(db)
    try:
        return con.execute(
            "SELECT b.boot, p.device_participant_id, e.session, x.name, "
            "e.planned_sets, e.planned_amount, e.planned_measure, e.planned_load_quarters, e.planned_load_unit, "
            "e.actual_sets, e.actual_amount, e.actual_measure, e.actual_load_quarters, e.actual_load_unit, "
            "e.revision, b.device_id FROM dosage_entries e "
            "JOIN dosage_participants p ON p.id = e.participant_ref JOIN dosage_boots b ON b.id = p.boot_ref "
            "JOIN dosage_exercises x ON x.id = e.exercise_ref ORDER BY b.id, p.device_participant_id, e.session, x.name"
        ).fetchall()
    finally:
        con.close()


def scalar(db, sql):
    con = sqlite3.connect(db)
    try:
        return con.execute(sql).fetchone()[0]
    finally:
        con.close()


def run(con_sql, db):
    con = sqlite3.connect(db)
    try:
        con.executescript(con_sql)
    finally:
        con.close()


def main():
    work = Path(tempfile.mkdtemp(prefix="dosage_e2e."))
    db = work / "cccare.db"
    plan = (2, 8, "reps", 0, "kg")  # Alex's starter Squat prescription

    print("1. Squat in two sessions, same prescription, different results")
    dev = Device(work)
    device_id, boot = dev.identity()
    check("first power-up counted, no device ID until the first sync", (device_id, boot) == ("00000000", 1))
    dev.new_session("Alex")
    dev.log_completed("Alex", "Squat", 2, 8, "reps", 0, "kg")
    dev.new_session("Alex")
    dev.log_completed("Alex", "Squat", 3, 10, "reps", 10, "kg")      # 2.5 kg
    check("device holds two Squat entries in sessions 1 and 2",
          "S1 Squat plan=2/8/reps/0/kg done=2/8/reps/0/kg" in dev.entries()
          and "S2 Squat plan=2/8/reps/0/kg done=3/10/reps/10/kg" in dev.entries(), dev.entries())
    rc, out, lcd = sync(dev, db)
    check("sync exits 0", rc == 0, out)
    check("Arduino shows 'USB sync done' after the ACK", lcd == ("USB sync done", "Saved on laptop"), lcd)
    check("receiver reports 2 new entries", "2 new, 0 corrected, 0 unchanged" in out, out)
    device_id, _ = dev.identity()
    check("device ID was created at the first sync", device_id != "00000000")
    rows = entries_in_db(db)
    check("database has both sessions with planned and actual values and units", [r[:15] for r in rows] == [
        (1, 1, 1, "Squat", *plan, 2, 8, "reps", 0, "kg", 1),
        (1, 1, 2, "Squat", *plan, 3, 10, "reps", 10, "kg", 1)], rows)
    check("records carry this device's ID", {r[15] for r in rows} == {device_id})
    dev.cmd("wait 3000")
    check("Arduino returns to the main menu afterwards", dev.screen()[0] == 0)

    print("2. Repeating the sync")
    rc, out, lcd = sync(dev, db)
    rc2, out2, _ = sync(dev, db)
    check("repeat syncs exit 0", rc == 0 and rc2 == 0, out + out2)
    check("nothing new, nothing corrected", "0 new, 0 corrected, 2 unchanged" in out2, out2)
    check("still exactly 2 entries (no duplicates)", len(entries_in_db(db)) == 2)
    check("3 transfers recorded", scalar(db, "SELECT COUNT(*) FROM dosage_transfers") == 3)

    print("3. Correcting one entry")
    dev.log_completed("Alex", "Squat", 3, 12, "reps", 20, "lb")      # session 2 is current: edits it
    check("device edited the session 2 entry in place (still 2 entries)",
          dev.entries().startswith("ENTRIES 2") and "S2 Squat plan=2/8/reps/0/kg done=3/12/reps/20/lb" in dev.entries(),
          dev.entries())
    rc, out, lcd = sync(dev, db)
    check("sync exits 0", rc == 0, out)
    check("receiver reports 1 corrected", "0 new, 1 corrected, 1 unchanged" in out, out)
    rows = entries_in_db(db)
    check("session 1 untouched", rows[0][:15] == (1, 1, 1, "Squat", *plan, 2, 8, "reps", 0, "kg", 1), rows[0])
    check("session 2 corrected (5 lb), prescription copy kept, revision 2",
          rows[1][:15] == (1, 1, 2, "Squat", *plan, 3, 12, "reps", 20, "lb", 2), rows[1])
    check("old values kept as revision 1",
          scalar(db, "SELECT actual_amount || '/' || actual_load_quarters || actual_load_unit "
                     "FROM dosage_entry_revisions") == "10/10kg")

    print("4. A failed laptop save leaves the records on the Arduino")
    dev.log_completed("Jamie", "Lunges", 1, 10, "reps", 0, "kg")
    run("CREATE TRIGGER fail BEFORE INSERT ON dosage_transfers BEGIN SELECT RAISE(ABORT, 'disk I/O error'); END;", db)
    rc, out, lcd = sync(dev, db)
    check("sync exits 1", rc == 1, out)
    check("receiver says nothing was saved", "nothing saved" in out, out)
    check("Arduino shows 'Laptop save fail'", lcd == ("Laptop save fail", "Retry from menu"), lcd)
    check("no Jamie entry in the database", len(entries_in_db(db)) == 2)
    check("Arduino still holds all 3 entries", dev.entries().startswith("ENTRIES 3"), dev.entries())
    run("DROP TRIGGER fail;", db)
    dev.cmd("wait 3000")
    rc, out, lcd = sync(dev, db)
    check("retry exits 0 and saves Jamie", rc == 0 and "1 new, 0 corrected, 2 unchanged" in out, out)
    check("Arduino shows 'USB sync done'", lcd == ("USB sync done", "Saved on laptop"), lcd)

    print("5. Database locked by another program")
    dev.cmd("wait 3000")
    locker = sqlite3.connect(db, isolation_level=None)
    locker.execute("BEGIN EXCLUSIVE")
    rc, out, lcd = sync(dev, db)
    locker.execute("ROLLBACK")
    locker.close()
    check("sync exits 1 and reports the lock", rc == 1 and "locked" in out, out)
    check("Arduino shows 'Laptop save fail' (it was told, not left to time out)",
          lcd == ("Laptop save fail", "Retry from menu"), (lcd, out))
    dev.cmd("wait 3000")
    rc, out, lcd = sync(dev, db)
    check("works once the lock is released", rc == 0 and lcd[0] == "USB sync done", out)

    print("6. A row lost in transit")
    dev.stop()
    dev = Device(work, "--drop-completed-once")
    check("power cycle: same device ID, boot 2", dev.identity() == (device_id, 2), dev.identity())
    dev.new_session("Alex")
    dev.log_completed("Alex", "Squat", 1, 5, "reps", 0, "kg")
    started = time.monotonic()
    rc, out, lcd = sync(dev, db)
    check("sync exits 1", rc == 1, out)
    check("receiver fails fast, while the Arduino still waits", time.monotonic() - started < 9, out)
    check("Arduino shows 'Laptop save fail'", lcd[0] == "Laptop save fail", lcd)
    check("database unchanged (3 entries)", len(entries_in_db(db)) == 3)
    dev.cmd("wait 3000")
    rc, out, lcd = sync(dev, db)
    check("retry exits 0", rc == 0, out)

    print("7. After a reset, reused participant, session and transfer numbers stay separate")
    rows = entries_in_db(db)
    check("boot 2's Alex session 1 is a new record", len(rows) == 4 and rows[3][:4] == (2, 1, 1, "Squat"), rows)
    check("boot 1's Alex session 1 is untouched", rows[0][:15] == (1, 1, 1, "Squat", *plan, 2, 8, "reps", 0, "kg", 1))
    check("both power-ups belong to one device", scalar(db, "SELECT COUNT(DISTINCT device_id) FROM dosage_boots") == 1)
    check("boot 2 reused transfer numbers boot 1 had used, and both were kept",
          scalar(db, "SELECT COUNT(*) FROM (SELECT transfer_id FROM dosage_transfers "
                     "GROUP BY transfer_id HAVING COUNT(DISTINCT boot_ref) = 2)") >= 1)

    print("8. No receiver running when A3 is tapped")
    dev.cmd("wait 3000")
    dev.cmd("usb 0")
    dev.cmd("touch")
    check("Arduino says no receiver", dev.lcd() == ("No USB receiver", "Open sync tool"), dev.lcd())
    dev.cmd("wait 3000")
    dev.cmd("usb 1")
    dev.cmd("touch")             # port open but nobody reading: bytes wait in the buffer
    dev.cmd("wait 10500")
    check("Arduino times out with 'No save reply'", dev.lcd()[0] == "No save reply", dev.lcd())
    dev.cmd("wait 3000")
    rc, out, lcd = sync(dev, db)  # the stale snapshot must not be ACKed in place of the new one
    check("next sync exits 0 and the Arduino accepts its ACK", rc == 0 and lcd[0] == "USB sync done", (out, lcd))

    print("9. Receiver waiting but nobody taps A3")
    dev.cmd("wait 3000")
    before = scalar(db, "SELECT COUNT(*) FROM dosage_transfers")
    rc, out, lcd = sync(dev, db, tap=False, wait=1)
    check("receiver exits 1 after its wait", rc == 1 and "timed out" in out, out)
    check("nothing written", scalar(db, "SELECT COUNT(*) FROM dosage_transfers") == before)

    print("10. Laptop stops reading part way (USB stalled)")
    dev.stop()
    dev = Device(work, "--stall-writes")
    rx = Receiver(dev.port, db, wait=3)
    started = time.monotonic()
    dev.cmd("touch")  # the firmware retries writes for up to 8 s (real time) inside the tap
    check("Arduino gives up with 'USB send failed' after about 8 s",
          dev.lcd() == ("USB send failed", "Retry from menu") and 7.5 < time.monotonic() - started < 12,
          (dev.lcd(), time.monotonic() - started))
    rc, out = rx.finish()
    check("receiver saves nothing", rc == 1, out)

    print("11. EEPROM erased: the board picks a new device ID")
    dev.stop()
    (work / "eeprom.bin").unlink()
    dev = Device(work)
    rc, out, lcd = sync(dev, db)
    new_id, new_boot = dev.identity()
    check("sync exits 0", rc == 0, out)
    check("new device ID, boot 1 again", new_id not in ("00000000", device_id) and new_boot == 1, (new_id, new_boot))
    check("earlier records untouched", entries_in_db(db)[0][:15] == (1, 1, 1, "Squat", *plan, 2, 8, "reps", 0, "kg", 1))

    print("12. Old 1.0.7 firmware is refused, records kept")
    dev.stop()
    old = Device(work, binary="sim_dosage_device_107")
    old.log_completed("Alex", "Squat", 2, 8, "reps", 0, "kg")
    before = scalar(db, "SELECT COUNT(*) FROM dosage_entries")
    rc, out, lcd = sync(old, db)
    check("sync exits 1 and says to flash 1.0.8", rc == 1 and "ece198Codes108" in out, out)
    check("Arduino shows 'Laptop save fail'", lcd[0] == "Laptop save fail", lcd)
    check("database unchanged", scalar(db, "SELECT COUNT(*) FROM dosage_entries") == before)
    old.stop()

    print("13. CSV export")
    proc = subprocess.run([sys.executable, str(EXPORTER), "--db", str(db), "--out-dir", str(work / "out")],
                          capture_output=True, text=True)
    check("export exits 0", proc.returncode == 0, proc.stderr)
    completed = sorted((work / "out").glob("completed_*.csv"))
    plans = sorted((work / "out").glob("plans_*.csv"))
    with open(completed[0], newline="") as f:
        table = list(csv.DictReader(f))
    check("one CSV row per database entry", len(table) == scalar(db, "SELECT COUNT(*) FROM dosage_entries"))
    s2 = [r for r in table if r["power_up"] == "1" and r["participant"] == "Alex" and r["session"] == "2"]
    check("corrected Squat row exports planned and actual values with units", len(s2) == 1 and (
        s2[0]["planned_sets"], s2[0]["planned_amount"], s2[0]["planned_measure"], s2[0]["planned_load"],
        s2[0]["planned_unit"], s2[0]["actual_sets"], s2[0]["actual_amount"], s2[0]["actual_measure"],
        s2[0]["actual_load"], s2[0]["actual_unit"], s2[0]["revision"]) ==
        ("2", "8", "reps", "0", "kg", "3", "12", "reps", "5", "lb", "2"), s2)
    with open(plans[0], newline="") as f:
        plan_rows = list(csv.DictReader(f))
    check("plans file lists current prescriptions", any(
        r["participant"] == "Alex" and r["exercise"] == "Squat" and r["sets"] == "2" for r in plan_rows))

    print()
    print(f"{PASS} passed, {FAIL} failed")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
