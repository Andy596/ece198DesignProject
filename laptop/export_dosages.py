#!/usr/bin/env python3
"""Export saved dosage records from SQLite to CSV files for Excel or OneDrive.

Writes two files to the output folder:
  completed_<time>.csv  one row per completed exercise per session, with the
                        prescription saved at the time and the actual values
  plans_<time>.csv      the current prescription for every assigned exercise
"""

import argparse
import csv
from datetime import datetime
from pathlib import Path
import sqlite3
import sys

import dosage_db

DEFAULT_DB = Path(__file__).resolve().parent / "session_log.db"
DEFAULT_OUT = Path(__file__).resolve().parent / "exports"

COMPLETED_QUERY = """
SELECT b.device_id, b.boot, p.device_participant_id, e.participant_name, e.session, x.name,
       e.planned_sets, e.planned_amount, e.planned_measure, e.planned_load_quarters, e.planned_load_unit,
       e.actual_sets, e.actual_amount, e.actual_measure, e.actual_load_quarters, e.actual_load_unit,
       e.revision, tf.received_at, tl.received_at
FROM dosage_entries e
JOIN dosage_participants p ON p.id = e.participant_ref
JOIN dosage_boots b ON b.id = p.boot_ref
JOIN dosage_exercises x ON x.id = e.exercise_ref
JOIN dosage_transfers tf ON tf.id = e.first_transfer_ref
JOIN dosage_transfers tl ON tl.id = e.last_transfer_ref
ORDER BY b.device_id, b.boot, p.device_participant_id, e.session, x.name
"""
COMPLETED_HEADER = (
    "device_id", "power_up", "participant_id", "participant", "session", "exercise",
    "planned_sets", "planned_amount", "planned_measure", "planned_load", "planned_unit",
    "actual_sets", "actual_amount", "actual_measure", "actual_load", "actual_unit",
    "revision", "first_synced", "last_synced",
)

PLANS_QUERY = """
SELECT b.device_id, b.boot, p.device_participant_id, p.name, p.on_device, x.name,
       pl.sets, pl.amount, pl.measure, pl.load_quarters, pl.load_unit, t.received_at
FROM dosage_plans pl
JOIN dosage_participants p ON p.id = pl.participant_ref
JOIN dosage_boots b ON b.id = p.boot_ref
JOIN dosage_exercises x ON x.id = pl.exercise_ref
JOIN dosage_transfers t ON t.id = pl.transfer_ref
ORDER BY b.device_id, b.boot, p.device_participant_id, x.name
"""
PLANS_HEADER = (
    "device_id", "power_up", "participant_id", "participant", "on_device", "exercise",
    "sets", "amount", "measure", "load", "unit", "synced",
)


def _blank(value):
    return "" if value is None else value


def write_csv(path, header, rows):
    with open(path, "x", newline="", encoding="utf-8") as out:  # never overwrite
        writer = csv.writer(out)
        writer.writerow(header)
        writer.writerows(rows)


def export(db_path, out_dir):
    if not Path(db_path).exists():
        raise FileNotFoundError(f"no database at {db_path} yet. Run a sync first.")
    db = dosage_db.connect(str(db_path))
    try:
        completed = []
        for r in db.execute(COMPLETED_QUERY):
            r = list(r)
            r[9] = dosage_db.quarters_to_load(r[9])
            r[14] = dosage_db.quarters_to_load(r[14])
            completed.append([_blank(v) for v in r])
        plans = []
        for r in db.execute(PLANS_QUERY):
            r = list(r)
            r[4] = "yes" if r[4] else "no"
            r[9] = dosage_db.quarters_to_load(r[9])
            plans.append([_blank(v) for v in r])
    finally:
        db.close()

    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    completed_path = out_dir / f"completed_{stamp}.csv"
    plans_path = out_dir / f"plans_{stamp}.csv"
    write_csv(completed_path, COMPLETED_HEADER, completed)
    write_csv(plans_path, PLANS_HEADER, plans)
    return completed_path, len(completed), plans_path, len(plans)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    try:
        completed_path, n_completed, plans_path, n_plans = export(args.db, args.out_dir)
    except (OSError, sqlite3.Error) as error:
        print(f"Export failed: {error}", file=sys.stderr)
        return 1
    print(f"Exported {n_completed} completed entries to {completed_path}")
    print(f"Exported {n_plans} current plans to {plans_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
