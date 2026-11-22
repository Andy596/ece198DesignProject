"""SQLite storage for dosage snapshots from the Session Log Station (firmware 1.0.8+).

A snapshot is the device's full state: current plans plus every completed entry
still in its RAM. Importing it is idempotent:

* Records are keyed by (device ID, boot, device participant ID, session,
  exercise). Participant IDs, sessions and transfer IDs restart after a reset,
  so the device ID and power-up counter (both from EEPROM) are part of every key.
* A completed entry seen again with the same values is left alone. Different
  values are a correction: the entry is updated and the old values are kept in
  dosage_entry_revisions.
* Records missing from a later snapshot are never deleted (the device forgets
  everything on reset; the laptop keeps the history). Plans are the exception:
  they describe the current prescription, so they mirror the latest snapshot.
* The whole import is one transaction. The caller sends ACK only after it
  commits.
"""

import csv
import re
import sqlite3
from dataclasses import dataclass, field
from datetime import datetime
from typing import Dict, List, Optional, Sequence, Tuple

SESSION_COLUMNS = (
    "record", "participant_id", "participant", "exercise", "session",
    "planned_sets", "planned_amount", "planned_measure", "planned_load", "planned_unit",
    "actual_sets", "actual_amount", "actual_measure", "actual_load", "actual_unit",
)
MAX_ROWS = 624  # 10 profiles x (55 catalog + 1 starter) plans + 64 completed entries
BEGIN_FRAME = re.compile(r"DOSAGE_BEGIN ([0-9]{1,10}) ([0-9]{1,3}) ([0-9A-F]{8}) ([0-9]{1,10})")
LEGACY_BEGIN_FRAME = re.compile(r"DOSAGE_BEGIN ([0-9]{1,10}) ([0-9]{1,3})")
LOAD_TEXT = re.compile(r"([0-9]{1,3})(?:\.(25|5|75))?")
_QUARTERS = {None: 0, "25": 1, "5": 2, "75": 3}

SCHEMA = """
PRAGMA foreign_keys = ON;

-- One row per power-up of one board. device_id is random per board (EEPROM);
-- boot counts power-ups. Every other record hangs off this.
CREATE TABLE IF NOT EXISTS dosage_boots (
  id               INTEGER PRIMARY KEY,
  device_id        TEXT    NOT NULL,
  boot             INTEGER NOT NULL,
  first_seen_at    TEXT    NOT NULL,
  last_transfer_id INTEGER NOT NULL,  -- transfer IDs only increase within a boot
  UNIQUE (device_id, boot)
);

CREATE TABLE IF NOT EXISTS dosage_transfers (
  id               INTEGER PRIMARY KEY,
  boot_ref         INTEGER NOT NULL REFERENCES dosage_boots(id),
  transfer_id      INTEGER NOT NULL,
  received_at      TEXT    NOT NULL,
  row_count        INTEGER NOT NULL,
  new_entries      INTEGER NOT NULL,
  corrected_entries INTEGER NOT NULL,
  raw_snapshot     TEXT    NOT NULL,  -- exactly what the device sent, for auditing
  UNIQUE (boot_ref, transfer_id)
);

-- A participant as the device knew them during one boot. The device has no
-- persistent profiles, so the same person after a reset is a new row; export
-- shows names so staff can match them up.
CREATE TABLE IF NOT EXISTS dosage_participants (
  id                    INTEGER PRIMARY KEY,
  boot_ref              INTEGER NOT NULL REFERENCES dosage_boots(id),
  device_participant_id INTEGER NOT NULL,
  name                  TEXT    NOT NULL,
  on_device             INTEGER NOT NULL,  -- 0 once deleted from the device
  UNIQUE (boot_ref, device_participant_id)
);

-- Exercise labels exactly as the device sends them.
CREATE TABLE IF NOT EXISTS dosage_exercises (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE
);

-- Current prescription for each assigned exercise. NULL dosage = assigned
-- without a prescription yet. Loads are stored in quarter units (exact).
CREATE TABLE IF NOT EXISTS dosage_plans (
  participant_ref INTEGER NOT NULL REFERENCES dosage_participants(id),
  exercise_ref    INTEGER NOT NULL REFERENCES dosage_exercises(id),
  sets            INTEGER,
  amount          INTEGER,
  measure         TEXT CHECK (measure IN ('reps', 'sec')),
  load_quarters   INTEGER,
  load_unit       TEXT CHECK (load_unit IN ('kg', 'lb')),
  transfer_ref    INTEGER NOT NULL REFERENCES dosage_transfers(id),
  PRIMARY KEY (participant_ref, exercise_ref)
);

-- One completed exercise in one session, with the prescription copied when it
-- was first logged on the device.
CREATE TABLE IF NOT EXISTS dosage_entries (
  id                    INTEGER PRIMARY KEY,
  participant_ref       INTEGER NOT NULL REFERENCES dosage_participants(id),
  exercise_ref          INTEGER NOT NULL REFERENCES dosage_exercises(id),
  session               INTEGER NOT NULL,
  participant_name      TEXT    NOT NULL,  -- name stored with the entry on the device
  planned_sets          INTEGER,
  planned_amount        INTEGER,
  planned_measure       TEXT CHECK (planned_measure IN ('reps', 'sec')),
  planned_load_quarters INTEGER,
  planned_load_unit     TEXT CHECK (planned_load_unit IN ('kg', 'lb')),
  actual_sets           INTEGER NOT NULL,
  actual_amount         INTEGER NOT NULL,
  actual_measure        TEXT    NOT NULL CHECK (actual_measure IN ('reps', 'sec')),
  actual_load_quarters  INTEGER NOT NULL,
  actual_load_unit      TEXT    NOT NULL CHECK (actual_load_unit IN ('kg', 'lb')),
  revision              INTEGER NOT NULL DEFAULT 1,
  first_transfer_ref    INTEGER NOT NULL REFERENCES dosage_transfers(id),
  last_transfer_ref     INTEGER NOT NULL REFERENCES dosage_transfers(id),
  UNIQUE (participant_ref, session, exercise_ref)
);

-- Values an entry had before a correction replaced them.
CREATE TABLE IF NOT EXISTS dosage_entry_revisions (
  id                    INTEGER PRIMARY KEY,
  entry_ref             INTEGER NOT NULL REFERENCES dosage_entries(id),
  revision              INTEGER NOT NULL,
  planned_sets          INTEGER,
  planned_amount        INTEGER,
  planned_measure       TEXT,
  planned_load_quarters INTEGER,
  planned_load_unit     TEXT,
  actual_sets           INTEGER NOT NULL,
  actual_amount         INTEGER NOT NULL,
  actual_measure        TEXT    NOT NULL,
  actual_load_quarters  INTEGER NOT NULL,
  actual_load_unit      TEXT    NOT NULL,
  replaced_by_transfer  INTEGER NOT NULL REFERENCES dosage_transfers(id),
  UNIQUE (entry_ref, revision)
);
"""

DOSAGE_FIELDS = ("sets", "amount", "measure", "load_quarters", "load_unit")


class SnapshotError(ValueError):
    """The snapshot is malformed; nothing was saved."""


class IdentityError(ValueError):
    """The snapshot's device identity conflicts with what is already stored."""


@dataclass(frozen=True)
class Dosage:
    sets: int
    amount: int
    measure: str
    load_quarters: int
    load_unit: str

    def as_tuple(self) -> Tuple[int, int, str, int, str]:
        return (self.sets, self.amount, self.measure, self.load_quarters, self.load_unit)


@dataclass(frozen=True)
class PlanRow:
    participant_id: int
    name: str
    exercise: str            # "" for a participant with no exercises
    dosage: Optional[Dosage]


@dataclass(frozen=True)
class CompletedRow:
    participant_id: int
    name: str
    exercise: str
    session: int
    planned: Optional[Dosage]
    actual: Dosage


@dataclass
class Snapshot:
    transfer_id: int
    device_id: str
    boot: int
    plans: List[PlanRow] = field(default_factory=list)
    completed: List[CompletedRow] = field(default_factory=list)
    raw: str = ""


@dataclass
class ImportResult:
    new_entries: int = 0
    corrected_entries: int = 0
    unchanged_entries: int = 0
    missing_entries: int = 0   # stored for this boot but absent from the snapshot
    participants: int = 0
    plans: int = 0


# --- Parsing -------------------------------------------------------------------

def load_to_quarters(text: str) -> int:
    match = LOAD_TEXT.fullmatch(text)
    if not match:
        raise SnapshotError(f"invalid load {text!r}")
    return int(match.group(1)) * 4 + _QUARTERS[match.group(2)]


def quarters_to_load(quarters: Optional[int]) -> str:
    if quarters is None:
        return ""
    whole, part = divmod(quarters, 4)
    return f"{whole}{('', '.25', '.5', '.75')[part]}"


def parse_dosage(fields: Sequence[str], required: bool) -> Optional[Dosage]:
    """Five CSV fields -> Dosage. All empty means "no dosage" (allowed unless required)."""
    if not any(fields):
        if required:
            raise SnapshotError("completed row without actual values")
        return None
    sets, amount, measure, load, unit = fields
    if not (sets.isdigit() and 1 <= int(sets) <= 99):
        raise SnapshotError(f"invalid sets {sets!r}")
    if not (amount.isdigit() and 1 <= int(amount) <= 999):
        raise SnapshotError(f"invalid amount {amount!r}")
    if measure not in ("reps", "sec"):
        raise SnapshotError(f"invalid measure {measure!r}")
    if unit not in ("kg", "lb"):
        raise SnapshotError(f"invalid load unit {unit!r}")
    return Dosage(int(sets), int(amount), measure, load_to_quarters(load), unit)


def _parse_positive(text: str, what: str) -> int:
    if not (text.isdigit() and len(text) <= 10 and 0 < int(text) <= 0xFFFFFFFF):
        raise SnapshotError(f"invalid {what} {text!r}")
    return int(text)


def parse_begin(line: str) -> Tuple[int, int, str, int]:
    """Returns (transfer_id, row_count, device_id, boot) or raises."""
    match = BEGIN_FRAME.fullmatch(line)
    if match:
        transfer_id, rows, device_id, boot = match.groups()
        if device_id in ("00000000", "FFFFFFFF"):
            raise IdentityError("the device sent no device ID")
        return int(transfer_id), int(rows), device_id, int(boot)
    if LEGACY_BEGIN_FRAME.fullmatch(line):
        raise IdentityError(
            "this firmware sends no device identity (1.0.7 or older). Flash ece198Codes108 "
            "so records stay distinct across resets.")
    raise SnapshotError(f"not a snapshot start: {line!r}")


def parse_snapshot(begin: str, header: str, rows: Sequence[str], end: str) -> Snapshot:
    """Validates a complete snapshot. Raises SnapshotError/IdentityError; never partial."""
    transfer_id, row_count, device_id, boot = parse_begin(begin)
    if transfer_id == 0:
        raise SnapshotError("transfer ID 0")
    if boot == 0:
        raise IdentityError("boot counter 0")
    if row_count > MAX_ROWS:
        raise SnapshotError(f"too many rows ({row_count})")
    if tuple(header.split(",")) != SESSION_COLUMNS:
        raise SnapshotError("unexpected CSV header")
    if len(rows) != row_count:
        raise SnapshotError(f"expected {row_count} rows, got {len(rows)}")
    if end != f"DOSAGE_END {transfer_id}":
        raise SnapshotError("the snapshot is incomplete")

    snap = Snapshot(transfer_id, device_id, boot)
    names: Dict[int, str] = {}
    plan_keys = set()
    completed_keys = set()
    placeholder = set()
    for line in rows:
        try:
            parsed = list(csv.reader([line], strict=True))
        except csv.Error as error:
            raise SnapshotError(f"malformed row: {error}") from None
        if len(parsed) != 1 or len(parsed[0]) != len(SESSION_COLUMNS):
            raise SnapshotError(f"malformed row {line!r}")
        record, pid_text, name, exercise, session = parsed[0][:5]
        planned_fields, actual_fields = parsed[0][5:10], parsed[0][10:]
        pid = _parse_positive(pid_text, "participant ID")
        if not name or len(name) > 12:
            raise SnapshotError(f"invalid participant name {name!r}")
        if len(exercise) > 31:
            raise SnapshotError(f"exercise name too long {exercise!r}")
        planned = parse_dosage(planned_fields, required=False)
        if planned is not None and not exercise:
            raise SnapshotError("prescribed dosage without an exercise")

        if record == "plan":
            if session or any(actual_fields):
                raise SnapshotError("plan row with session or actual values")
            if pid in names and names[pid] != name:
                raise SnapshotError(f"participant {pid} has two names")
            names[pid] = name
            if not exercise:
                if pid in placeholder or any(k[0] == pid for k in plan_keys):
                    raise SnapshotError(f"participant {pid} listed twice without exercises")
                placeholder.add(pid)
            else:
                if (pid, exercise) in plan_keys:
                    raise SnapshotError(f"duplicate plan for {pid} {exercise}")
                if pid in placeholder:
                    raise SnapshotError(f"participant {pid} listed with and without exercises")
                plan_keys.add((pid, exercise))
            snap.plans.append(PlanRow(pid, name, exercise, planned))
        elif record == "completed":
            if not exercise:
                raise SnapshotError("completed row without an exercise")
            session_number = _parse_positive(session, "session")
            actual = parse_dosage(actual_fields, required=True)
            key = (pid, session_number, exercise)
            if key in completed_keys:
                raise SnapshotError(f"duplicate completed entry {key}")
            completed_keys.add(key)
            snap.completed.append(CompletedRow(pid, name, exercise, session_number, planned, actual))
        else:
            raise SnapshotError(f"unknown record type {record!r}")

    snap.raw = "\n".join([begin, header, *rows, end]) + "\n"
    return snap


# --- Database --------------------------------------------------------------------

def connect(path: str, timeout: float = 5.0) -> sqlite3.Connection:
    db = sqlite3.connect(path, timeout=timeout, isolation_level=None)  # explicit transactions
    db.executescript(SCHEMA)
    return db


def _now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _exercise_ref(db: sqlite3.Connection, name: str) -> int:
    db.execute("INSERT OR IGNORE INTO dosage_exercises (name) VALUES (?)", (name,))
    return db.execute("SELECT id FROM dosage_exercises WHERE name = ?", (name,)).fetchone()[0]


def _dosage_values(dosage: Optional[Dosage]) -> Tuple:
    return dosage.as_tuple() if dosage is not None else (None,) * 5


def import_snapshot(db: sqlite3.Connection, snap: Snapshot) -> ImportResult:
    """Saves a validated snapshot in one transaction. Raises on any failure."""
    result = ImportResult()
    db.execute("BEGIN IMMEDIATE")
    try:
        now = _now()
        row = db.execute("SELECT id, last_transfer_id FROM dosage_boots WHERE device_id = ? AND boot = ?",
                         (snap.device_id, snap.boot)).fetchone()
        if row is None:
            boot_ref = db.execute(
                "INSERT INTO dosage_boots (device_id, boot, first_seen_at, last_transfer_id) "
                "VALUES (?, ?, ?, ?)", (snap.device_id, snap.boot, now, snap.transfer_id)).lastrowid
        else:
            boot_ref, last = row
            if snap.transfer_id <= last:
                # Within one power-up the transfer ID only goes up. Going back means
                # this device ID and boot number were reused (e.g. EEPROM erased).
                raise IdentityError(
                    f"device {snap.device_id} boot {snap.boot} already sent transfer {last}; "
                    f"transfer {snap.transfer_id} would overwrite another session's records")
            db.execute("UPDATE dosage_boots SET last_transfer_id = ? WHERE id = ?",
                       (snap.transfer_id, boot_ref))

        transfer_ref = db.execute(
            "INSERT INTO dosage_transfers (boot_ref, transfer_id, received_at, row_count, "
            "new_entries, corrected_entries, raw_snapshot) VALUES (?, ?, ?, ?, 0, 0, ?)",
            (boot_ref, snap.transfer_id, now, len(snap.plans) + len(snap.completed), snap.raw)
        ).lastrowid

        # Participants: plan rows list everyone currently on the device; completed
        # rows can also name participants deleted since.
        on_device = {p.participant_id: p.name for p in snap.plans}
        everyone = dict((c.participant_id, c.name) for c in snap.completed)
        everyone.update(on_device)
        db.execute("UPDATE dosage_participants SET on_device = 0 WHERE boot_ref = ?", (boot_ref,))
        participant_refs: Dict[int, int] = {}
        for pid, name in everyone.items():
            present = 1 if pid in on_device else 0
            db.execute(
                "INSERT INTO dosage_participants (boot_ref, device_participant_id, name, on_device) "
                "VALUES (?, ?, ?, ?) ON CONFLICT (boot_ref, device_participant_id) "
                "DO UPDATE SET name = CASE WHEN excluded.on_device = 1 THEN excluded.name ELSE name END, "
                "on_device = excluded.on_device", (boot_ref, pid, name, present))
            participant_refs[pid] = db.execute(
                "SELECT id FROM dosage_participants WHERE boot_ref = ? AND device_participant_id = ?",
                (boot_ref, pid)).fetchone()[0]
        result.participants = len(on_device)

        # Plans mirror the device for this boot.
        db.execute("DELETE FROM dosage_plans WHERE participant_ref IN "
                   "(SELECT id FROM dosage_participants WHERE boot_ref = ?)", (boot_ref,))
        for plan in snap.plans:
            if not plan.exercise:
                continue
            db.execute(
                "INSERT INTO dosage_plans (participant_ref, exercise_ref, sets, amount, measure, "
                "load_quarters, load_unit, transfer_ref) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (participant_refs[plan.participant_id], _exercise_ref(db, plan.exercise),
                 *_dosage_values(plan.dosage), transfer_ref))
            result.plans += 1

        # Completed entries: insert, correct, or leave alone. Never delete.
        seen = set()
        for entry in snap.completed:
            pref = participant_refs[entry.participant_id]
            eref = _exercise_ref(db, entry.exercise)
            values = (entry.name, *_dosage_values(entry.planned), *entry.actual.as_tuple())
            existing = db.execute(
                "SELECT id, revision, participant_name, planned_sets, planned_amount, planned_measure, "
                "planned_load_quarters, planned_load_unit, actual_sets, actual_amount, actual_measure, "
                "actual_load_quarters, actual_load_unit FROM dosage_entries "
                "WHERE participant_ref = ? AND session = ? AND exercise_ref = ?",
                (pref, entry.session, eref)).fetchone()
            if existing is None:
                entry_id = db.execute(
                    "INSERT INTO dosage_entries (participant_ref, exercise_ref, session, participant_name, "
                    "planned_sets, planned_amount, planned_measure, planned_load_quarters, planned_load_unit, "
                    "actual_sets, actual_amount, actual_measure, actual_load_quarters, actual_load_unit, "
                    "first_transfer_ref, last_transfer_ref) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (pref, eref, entry.session, *values, transfer_ref, transfer_ref)).lastrowid
                result.new_entries += 1
            else:
                entry_id, revision = existing[0], existing[1]
                if tuple(existing[2:]) != values:
                    db.execute(
                        "INSERT INTO dosage_entry_revisions (entry_ref, revision, planned_sets, "
                        "planned_amount, planned_measure, planned_load_quarters, planned_load_unit, "
                        "actual_sets, actual_amount, actual_measure, actual_load_quarters, actual_load_unit, "
                        "replaced_by_transfer) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (entry_id, revision, *existing[3:], transfer_ref))
                    db.execute(
                        "UPDATE dosage_entries SET participant_name = ?, planned_sets = ?, planned_amount = ?, "
                        "planned_measure = ?, planned_load_quarters = ?, planned_load_unit = ?, "
                        "actual_sets = ?, actual_amount = ?, actual_measure = ?, actual_load_quarters = ?, "
                        "actual_load_unit = ?, revision = revision + 1, last_transfer_ref = ? WHERE id = ?",
                        (*values, transfer_ref, entry_id))
                    result.corrected_entries += 1
                else:
                    db.execute("UPDATE dosage_entries SET last_transfer_ref = ? WHERE id = ?",
                               (transfer_ref, entry_id))
                    result.unchanged_entries += 1
            seen.add(entry_id)

        stored = [r[0] for r in db.execute(
            "SELECT e.id FROM dosage_entries e JOIN dosage_participants p ON p.id = e.participant_ref "
            "WHERE p.boot_ref = ?", (boot_ref,))]
        result.missing_entries = sum(1 for entry_id in stored if entry_id not in seen)

        db.execute("UPDATE dosage_transfers SET new_entries = ?, corrected_entries = ? WHERE id = ?",
                   (result.new_entries, result.corrected_entries, transfer_ref))
        db.execute("COMMIT")
    except BaseException:
        db.execute("ROLLBACK")
        raise
    return result
