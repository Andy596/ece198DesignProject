#!/usr/bin/env python3
"""Unit tests for dosage_db (parsing and SQLite import). Run: python3 test_dosage_db.py"""

import os
import sqlite3
import tempfile
import threading
import unittest

import dosage_db as d

HEADER = ",".join(d.SESSION_COLUMNS)


def plan(pid, name, exercise, dosage="2,8,reps,0,kg"):
    return f'plan,{pid},"{name}","{exercise}",,{dosage},,,,,'


def done(pid, name, exercise, session, actual, planned="2,8,reps,0,kg"):
    return f'completed,{pid},"{name}","{exercise}",{session},{planned},{actual}'


def snapshot(rows, transfer=1, device="A1B2C3D4", boot=1, count=None, end=None):
    count = len(rows) if count is None else count
    end = f"DOSAGE_END {transfer}" if end is None else end
    return d.parse_snapshot(f"DOSAGE_BEGIN {transfer} {count} {device} {boot}", HEADER, rows, end)


DEMO_PLANS = [plan(1, "Alex", "Squat"), plan(2, "Jamie", "Lunges", "1,10,reps,0,kg"),
              plan(3, "Taylor", "Push-ups", "2,6,reps,0,kg")]


class DbTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "t.db")
        self.db = d.connect(self.path)

    def tearDown(self):
        self.db.close()
        self.tmp.cleanup()

    def sync(self, rows, **kw):
        return d.import_snapshot(self.db, snapshot(rows, **kw))

    def q(self, sql, *args):
        return self.db.execute(sql, args).fetchall()

    def entries(self):
        return self.q(
            "SELECT b.boot, p.device_participant_id, e.session, x.name, e.planned_sets, e.planned_amount, "
            "e.planned_measure, e.planned_load_quarters, e.planned_load_unit, e.actual_sets, e.actual_amount, "
            "e.actual_measure, e.actual_load_quarters, e.actual_load_unit, e.revision FROM dosage_entries e "
            "JOIN dosage_participants p ON p.id = e.participant_ref JOIN dosage_boots b ON b.id = p.boot_ref "
            "JOIN dosage_exercises x ON x.id = e.exercise_ref ORDER BY b.boot, p.device_participant_id, e.session")


class TestParsing(unittest.TestCase):
    def test_load_quarters(self):
        for text, q in [("0", 0), ("2.5", 10), ("10.25", 41), ("999.75", 3999), ("7.75", 31)]:
            self.assertEqual(d.load_to_quarters(text), q)
            self.assertEqual(d.quarters_to_load(q), text)
        for bad in ["", "1.0", "1.1", "1000", "-1", "1.", ".5", "1.50", "a", "1,5"]:
            with self.assertRaises(d.SnapshotError, msg=bad):
                d.load_to_quarters(bad)
        self.assertEqual(d.quarters_to_load(None), "")

    def test_good_snapshot(self):
        s = snapshot(DEMO_PLANS + [done(1, "Alex", "Squat", 1, "3,10,reps,2.5,lb")])
        self.assertEqual((s.transfer_id, s.device_id, s.boot), (1, "A1B2C3D4", 1))
        self.assertEqual(len(s.plans), 3)
        c = s.completed[0]
        self.assertEqual((c.participant_id, c.session, c.exercise), (1, 1, "Squat"))
        self.assertEqual(c.planned, d.Dosage(2, 8, "reps", 0, "kg"))
        self.assertEqual(c.actual, d.Dosage(3, 10, "reps", 10, "lb"))
        self.assertIn("DOSAGE_END 1", s.raw)

    def test_names_with_commas_and_apostrophes(self):
        s = snapshot([plan(4, "Lee, Jo", "Farmer's carry"), done(4, "Lee, Jo", "Farmer's carry", 2, "1,30,sec,10,kg")])
        self.assertEqual(s.plans[0].name, "Lee, Jo")
        self.assertEqual(s.completed[0].actual.measure, "sec")

    def test_participant_without_exercises_and_unset_dosage(self):
        s = snapshot([plan(5, "Sam", "", ",,,,"), plan(6, "Kim", "Plank", ",,,,"),
                      done(6, "Kim", "Plank", 3, "1,45,sec,0,kg", planned=",,,,")])
        self.assertIsNone(s.plans[0].dosage)
        self.assertIsNone(s.plans[1].dosage)
        self.assertIsNone(s.completed[0].planned)

    def test_legacy_firmware_rejected(self):
        with self.assertRaisesRegex(d.IdentityError, "ece198Codes108"):
            d.parse_snapshot("DOSAGE_BEGIN 1 1", HEADER, [plan(1, "A", "Squat")], "DOSAGE_END 1")

    def test_identity_values(self):
        for begin in ["DOSAGE_BEGIN 1 0 00000000 1", "DOSAGE_BEGIN 1 0 FFFFFFFF 1", "DOSAGE_BEGIN 1 0 A1B2C3D4 0"]:
            with self.assertRaises(d.IdentityError, msg=begin):
                d.parse_snapshot(begin, HEADER, [], "DOSAGE_END 1")

    def test_frame_errors(self):
        rows = DEMO_PLANS
        bad_frames = [
            ("DOSAGE_BEGIN 1 3 a1b2c3d4 1", HEADER, rows, "DOSAGE_END 1"),     # lowercase hex
            ("DOSAGE_BEGIN 1 3 A1B2C3D4 1 9", HEADER, rows, "DOSAGE_END 1"),   # extra field
            ("DOSAGE_BEGIN 0 3 A1B2C3D4 1", HEADER, rows, "DOSAGE_END 0"),     # transfer 0
            ("DOSAGE_BEGIN 1 4 A1B2C3D4 1", HEADER, rows, "DOSAGE_END 1"),     # count mismatch
            ("DOSAGE_BEGIN 1 2 A1B2C3D4 1", HEADER, rows, "DOSAGE_END 1"),
            ("DOSAGE_BEGIN 1 3 A1B2C3D4 1", HEADER, rows, "DOSAGE_END 2"),     # wrong end
            ("DOSAGE_BEGIN 1 3 A1B2C3D4 1", HEADER, rows, ""),
            ("DOSAGE_BEGIN 1 3 A1B2C3D4 1", HEADER.replace("session", "visit"), rows, "DOSAGE_END 1"),
            ("DOSAGE_BEGIN 1 625 A1B2C3D4 1", HEADER, rows, "DOSAGE_END 1"),   # too many rows
            ("XDOSAGE_BEGIN 1 3 A1B2C3D4 1", HEADER, rows, "DOSAGE_END 1"),
        ]
        for frame in bad_frames:
            with self.assertRaises(ValueError, msg=frame[0]):
                d.parse_snapshot(*frame)

    def test_row_errors(self):
        bad_rows = [
            'plan,1,"Alex","Squat",,2,8,reps,0,kg,,,,',               # 14 columns
            'plan,1,"Alex","Squat",,2,8,reps,0,kg,,,,,,',             # 16 columns
            'plan,0,"Alex","Squat",,2,8,reps,0,kg,,,,,',              # participant 0
            'plan,x,"Alex","Squat",,2,8,reps,0,kg,,,,,',
            'plan,-1,"Alex","Squat",,2,8,reps,0,kg,,,,,',
            'plan,1,"","Squat",,2,8,reps,0,kg,,,,,',                  # no name
            'plan,1,"ABCDEFGHIJKLM","Squat",,2,8,reps,0,kg,,,,,',     # 13-char name
            'plan,1,"Alex","",,2,8,reps,0,kg,,,,,',                   # dosage without exercise
            'plan,1,"Alex","Squat",3,2,8,reps,0,kg,,,,,',             # plan with session
            'plan,1,"Alex","Squat",,2,8,reps,0,kg,1,1,reps,0,kg',     # plan with actual
            'plan,1,"Alex","Squat",,0,8,reps,0,kg,,,,,',              # 0 sets
            'plan,1,"Alex","Squat",,100,8,reps,0,kg,,,,,',            # 100 sets
            'plan,1,"Alex","Squat",,2,0,reps,0,kg,,,,,',
            'plan,1,"Alex","Squat",,2,1000,reps,0,kg,,,,,',
            'plan,1,"Alex","Squat",,2,8,laps,0,kg,,,,,',
            'plan,1,"Alex","Squat",,2,8,reps,0,st,,,,,',
            'plan,1,"Alex","Squat",,2,8,reps,0.3,kg,,,,,',
            'plan,1,"Alex","Squat",,2,8,reps,,kg,,,,,',               # partly empty dosage
            'plan,1,"Alex","Squat",,2,,reps,0,kg,,,,,',
            'completed,1,"Alex","Squat",,2,8,reps,0,kg,1,1,reps,0,kg',     # no session
            'completed,1,"Alex","Squat",0,2,8,reps,0,kg,1,1,reps,0,kg',
            'completed,1,"Alex","Squat",1,2,8,reps,0,kg,,,,,',             # no actual
            'completed,1,"Alex","",1,,,,,,1,1,reps,0,kg',                  # no exercise
            'completed,1,"Alex","Squat",1,2,8,reps,0,kg,1,1,reps,0,',
            'done,1,"Alex","Squat",1,2,8,reps,0,kg,1,1,reps,0,kg',
            'plan,1,"Al"ex","Squat",,2,8,reps,0,kg,,,,,',             # broken quoting
            'plan,1,"Alex,"Squat",,2,8,reps,0,kg,,,,,',
            'plan,1,"' + "x" * 3 + '","' + "y" * 32 + '",,2,8,reps,0,kg,,,,,',  # 32-char exercise
        ]
        for row in bad_rows:
            with self.assertRaises(ValueError, msg=row):
                snapshot([row])

    def test_duplicates_and_conflicts(self):
        cases = [
            [plan(1, "Alex", "Squat"), plan(1, "Alex", "Squat")],
            [plan(1, "Alex", "Squat"), plan(1, "Alexa", "Plank")],
            [plan(1, "Alex", ""), plan(1, "Alex", "")],
            [plan(1, "Alex", ""), plan(1, "Alex", "Plank")],
            [plan(1, "Alex", "Plank"), plan(1, "Alex", "")],
            [done(1, "Alex", "Squat", 1, "1,1,reps,0,kg"), done(1, "Alex", "Squat", 1, "2,1,reps,0,kg")],
        ]
        for rows in cases:
            with self.assertRaises(d.SnapshotError, msg=rows):
                snapshot(rows)
        # Same exercise in different sessions or for different people is fine.
        snapshot([done(1, "Alex", "Squat", 1, "1,1,reps,0,kg"), done(1, "Alex", "Squat", 2, "1,1,reps,0,kg"),
                  done(2, "Jamie", "Squat", 1, "1,1,reps,0,kg")])

    def test_empty_device(self):
        s = snapshot([])
        self.assertEqual((s.plans, s.completed), ([], []))


class TestImport(DbTest):
    def test_two_sessions_same_prescription(self):
        # The handoff scenario: Squat in two sessions, same plan, different results.
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg"),
                             done(1, "Alex", "Squat", 2, "3,10,reps,2.5,kg")]
        r = self.sync(rows)
        self.assertEqual((r.new_entries, r.corrected_entries, r.unchanged_entries), (2, 0, 0))
        self.assertEqual(self.entries(), [
            (1, 1, 1, "Squat", 2, 8, "reps", 0, "kg", 2, 8, "reps", 0, "kg", 1),
            (1, 1, 2, "Squat", 2, 8, "reps", 0, "kg", 3, 10, "reps", 10, "kg", 1),
        ])

    def test_repeat_sync_no_duplicates(self):
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")]
        self.sync(rows, transfer=1)
        r = self.sync(rows, transfer=2)
        r = self.sync(rows, transfer=3)
        self.assertEqual((r.new_entries, r.corrected_entries, r.unchanged_entries), (0, 0, 1))
        self.assertEqual(len(self.entries()), 1)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_plans")[0][0], 3)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_participants")[0][0], 3)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_exercises")[0][0], 3)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_entry_revisions")[0][0], 0)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_transfers")[0][0], 3)

    def test_correction_updates_the_intended_record(self):
        base = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg"),
                             done(1, "Alex", "Squat", 2, "3,10,reps,2.5,kg"),
                             done(2, "Jamie", "Lunges", 2, "1,10,reps,0,kg", planned="1,10,reps,0,kg")]
        self.sync(base, transfer=1)
        corrected = list(base)
        corrected[4] = done(1, "Alex", "Squat", 2, "3,12,reps,5,lb")
        r = self.sync(corrected, transfer=2)
        self.assertEqual((r.new_entries, r.corrected_entries, r.unchanged_entries), (0, 1, 2))
        rows = self.entries()
        self.assertEqual(rows[0][9:], (2, 8, "reps", 0, "kg", 1))       # session 1 untouched
        self.assertEqual(rows[1][9:], (3, 12, "reps", 20, "lb", 2))     # session 2 corrected
        self.assertEqual(rows[2][9:], (1, 10, "reps", 0, "kg", 1))      # Jamie untouched
        rev = self.q("SELECT revision, actual_sets, actual_amount, actual_load_quarters, actual_load_unit "
                     "FROM dosage_entry_revisions")
        self.assertEqual(rev, [(1, 3, 10, 10, "kg")])                   # old values kept
        # Correct again, then repeat: one more revision, then stable.
        corrected[4] = done(1, "Alex", "Squat", 2, "4,12,reps,5,lb")
        self.sync(corrected, transfer=3)
        r = self.sync(corrected, transfer=4)
        self.assertEqual(r.corrected_entries, 0)
        self.assertEqual(self.entries()[1][-1], 3)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_entry_revisions")[0][0], 2)

    def test_reset_keeps_records_apart(self):
        # After a reset the device reuses participant 1, session 1 and transfer 1.
        before = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")]
        after = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "9,9,reps,9,kg")]
        self.sync(before, transfer=1, boot=1)
        r = self.sync(after, transfer=1, boot=2)
        self.assertEqual((r.new_entries, r.corrected_entries), (1, 0))
        rows = self.entries()
        self.assertEqual([(x[0], x[9]) for x in rows], [(1, 2), (2, 9)])
        self.assertEqual(r.missing_entries, 0)

    def test_two_devices_kept_apart(self):
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")]
        self.sync(rows, device="AAAAAAAA")
        r = self.sync(rows, device="BBBBBBBB")
        self.assertEqual(r.new_entries, 1)
        self.assertEqual(len(self.entries()), 2)

    def test_transfer_going_backwards_is_refused(self):
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")]
        self.sync(rows, transfer=5)
        for t in (5, 4, 1):
            with self.assertRaises(d.IdentityError):
                self.sync(DEMO_PLANS + [done(1, "Alex", "Squat", 1, "1,1,reps,0,kg")], transfer=t)
        self.assertEqual(self.entries()[0][9], 2)  # nothing overwritten
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_transfers")[0][0], 1)
        self.sync(rows, transfer=6)                # forward again is fine

    def test_missing_entries_are_kept(self):
        self.sync(DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")], transfer=1)
        r = self.sync(DEMO_PLANS, transfer=2)
        self.assertEqual(r.missing_entries, 1)
        self.assertEqual(len(self.entries()), 1)

    def test_plans_mirror_the_device(self):
        self.sync(DEMO_PLANS + [plan(1, "Alex", "Plank", "3,30,sec,0,kg")], transfer=1)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_plans")[0][0], 4)
        # Plank removed, Squat prescription changed, Taylor deleted from the device.
        r = self.sync([plan(1, "Alex", "Squat", "3,8,reps,5,kg"), plan(2, "Jamie", "Lunges")], transfer=2)
        self.assertEqual(r.plans, 2)
        got = self.q("SELECT p.name, x.name, pl.sets, pl.load_quarters FROM dosage_plans pl "
                     "JOIN dosage_participants p ON p.id = pl.participant_ref "
                     "JOIN dosage_exercises x ON x.id = pl.exercise_ref ORDER BY p.name")
        self.assertEqual(got, [("Alex", "Squat", 3, 20), ("Jamie", "Lunges", 2, 0)])
        self.assertEqual(self.q("SELECT name, on_device FROM dosage_participants ORDER BY name"),
                         [("Alex", 1), ("Jamie", 1), ("Taylor", 0)])

    def test_prescription_copy_is_independent_of_plan(self):
        # The plan changed after the entry was logged; the entry keeps its copy.
        self.sync([plan(1, "Alex", "Squat", "3,8,reps,5,kg"),
                   done(1, "Alex", "Squat", 1, "2,8,reps,0,kg", planned="2,8,reps,0,kg")])
        self.assertEqual(self.entries()[0][4:9], (2, 8, "reps", 0, "kg"))

    def test_deleted_participant_entries_arrive(self):
        # Entries keep their name after the profile is deleted on the device.
        r = self.sync([plan(1, "Alex", "Squat"), done(7, "Morgan", "Plank", 4, "1,30,sec,0,kg", planned=",,,,")])
        self.assertEqual(r.new_entries, 1)
        self.assertEqual(self.q("SELECT name, on_device FROM dosage_participants WHERE device_participant_id = 7"),
                         [("Morgan", 0)])

    def test_failed_write_saves_nothing(self):
        self.sync(DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")], transfer=1)
        # Simulate the disk failing part way through the import.
        self.db.execute("CREATE TRIGGER fail BEFORE INSERT ON dosage_entries "
                        "BEGIN SELECT RAISE(ABORT, 'disk I/O error'); END")
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "5,5,reps,0,kg"), done(1, "Alex", "Squat", 2, "1,1,reps,0,kg")]
        with self.assertRaises(sqlite3.Error):
            self.sync(rows, transfer=2)
        self.assertEqual(self.entries()[0][9], 2)                     # correction rolled back too
        self.assertEqual(self.q("SELECT last_transfer_id FROM dosage_boots")[0][0], 1)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_transfers")[0][0], 1)
        self.assertEqual(self.q("SELECT COUNT(*) FROM dosage_entry_revisions")[0][0], 0)
        self.db.execute("DROP TRIGGER fail")
        r = self.sync(rows, transfer=3)                               # retry succeeds
        self.assertEqual((r.new_entries, r.corrected_entries), (1, 1))

    def test_locked_database_fails_cleanly(self):
        # Another program (e.g. a DB browser) holds a write lock during the sync.
        other = sqlite3.connect(self.path, isolation_level=None)
        other.execute("BEGIN EXCLUSIVE")
        try:
            with self.assertRaises(sqlite3.OperationalError):
                db = d.connect(self.path, timeout=0.2)
                try:
                    d.import_snapshot(db, snapshot(DEMO_PLANS))
                finally:
                    db.close()
        finally:
            other.execute("ROLLBACK")
            other.close()
        self.assertEqual(self.sync(DEMO_PLANS, transfer=2).plans, 3)  # works once released

    def test_concurrent_receivers_do_not_corrupt(self):
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")]
        errors = []

        def worker(transfer):
            db = d.connect(self.path, timeout=10)
            try:
                d.import_snapshot(db, snapshot(rows, transfer=transfer))
            except d.IdentityError:
                pass  # a lower transfer ID that lost the race is refused
            except Exception as error:  # noqa: BLE001 - surface anything unexpected
                errors.append(error)
            finally:
                db.close()

        threads = [threading.Thread(target=worker, args=(t,)) for t in range(1, 9)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        self.assertEqual(len(self.entries()), 1)

    def test_maximum_snapshot(self):
        rows = []
        for pid in range(1, 11):
            for ex in range(56):
                rows.append(plan(pid, f"Person {pid}", f"Exercise {ex}", "99,999,sec,999.75,lb"))
        for i in range(64):
            rows.append(done(1 + i % 10, f"Person {1 + i % 10}", f"Exercise {i % 56}", 1 + i // 10, "99,999,reps,999.75,kg"))
        self.assertEqual(len(rows), d.MAX_ROWS)
        r = self.sync(rows)
        self.assertEqual((r.plans, r.new_entries), (560, 64))
        self.assertEqual(self.q("SELECT MAX(actual_load_quarters) FROM dosage_entries")[0][0], 3999)

    def test_raw_snapshot_kept(self):
        rows = DEMO_PLANS + [done(1, "Alex", "Squat", 1, "2,8,reps,0,kg")]
        self.sync(rows)
        raw = self.q("SELECT raw_snapshot FROM dosage_transfers")[0][0]
        self.assertTrue(raw.startswith("DOSAGE_BEGIN 1 4 A1B2C3D4 1\n" + HEADER))
        self.assertIn(rows[-1], raw)


if __name__ == "__main__":
    unittest.main(verbosity=1)
