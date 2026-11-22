-- Session Log Station database (SQLite). Safe to run on every start.

-- Participants the device can show. Labels are display names; use fictional
-- data for demos. Manage them with participants.bash (it checks the rules the
-- device needs: 1-12 characters from A-Z a-z 0-9 space . , ' -).
CREATE TABLE IF NOT EXISTS participants (
  id    INTEGER PRIMARY KEY,   -- 1..65535, the ID stored with each entry
  label TEXT NOT NULL
);

-- id is the index in exerciseCatalog in ece198Codes1.0.2/ece198Codes1.0.2.ino.
-- Rewritten on every start so the names always match the device.
CREATE TABLE IF NOT EXISTS exercises (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL
);

INSERT OR REPLACE INTO exercises (id, name) VALUES
  (0, 'Arm rainbows'),
  (1, 'Standing hip opener/closer'),
  (2, 'Airplane'),
  (3, 'Thoracic spine rotations'),
  (4, 'Wall angels'),
  (5, 'Scapular squeezes'),
  (6, 'Ball squats'),
  (7, 'Goblet squats'),
  (8, 'Step-ups'),
  (9, 'Lunges'),
  (10, 'Wall sits'),
  (11, 'Box squats'),
  (12, 'Push-ups'),
  (13, 'Chest press'),
  (14, 'Chest fly'),
  (15, 'Incline press'),
  (16, 'Cable crossovers'),
  (17, 'Wall push-ups'),
  (18, 'Shoulder press'),
  (19, 'Arnold press'),
  (20, 'Front raises'),
  (21, 'Close-grip push-ups'),
  (22, 'Romanian deadlifts'),
  (23, 'Good mornings'),
  (24, 'Kettlebell swings'),
  (25, 'Glute bridges'),
  (26, 'Deadlifts'),
  (27, 'Stability ball leg curls'),
  (28, 'Bent-over row'),
  (29, 'Seated row'),
  (30, 'Lat pulldown'),
  (31, 'Resistance band rows'),
  (32, 'TRX row'),
  (33, 'Reverse fly'),
  (34, 'Bicep curls'),
  (35, 'Hammer curls'),
  (36, 'Cable curls'),
  (37, 'Farmer''s carry'),
  (38, 'Suitcase carry'),
  (39, 'Plank'),
  (40, 'Side plank'),
  (41, 'Bird dog'),
  (42, 'Dead bug'),
  (43, 'Torso rotations'),
  (44, 'Mountain climbers'),
  (45, 'Leg raises'),
  (46, 'Pallof press'),
  (47, 'Hamstring stretch'),
  (48, 'Calf stretch'),
  (49, 'Chest doorway stretch'),
  (50, 'Upper trap stretch'),
  (51, 'Child''s pose'),
  (52, 'Hip flexor stretch'),
  (53, 'Cat-cow stretch'),
  (54, 'Neck side stretch');

-- Exercises assigned to a participant on the laptop. The device also shows
-- every exercise that already has a saved score.
CREATE TABLE IF NOT EXISTS assignments (
  participant_id INTEGER NOT NULL,
  exercise_id    INTEGER NOT NULL,
  PRIMARY KEY (participant_id, exercise_id)
);

-- No foreign keys on purpose: an entry for an unknown participant must still
-- be saved, or the device would never get its ACK. Exports flag unknown IDs.
-- technique holds the saved score (0-100). value is unused by 1.0.2 (always 0).
CREATE TABLE IF NOT EXISTS entries (
  id             INTEGER PRIMARY KEY,
  boot           INTEGER NOT NULL,  -- device power-up counter
  seq            INTEGER NOT NULL,  -- entry number within that boot
  participant_id INTEGER NOT NULL,
  exercise_id    INTEGER NOT NULL,
  value          INTEGER NOT NULL,
  technique      INTEGER NOT NULL,
  received_at    TEXT    NOT NULL,  -- laptop local time of the sync; the device has no clock
  UNIQUE (boot, seq)                -- re-syncing the same entry is a no-op
);

CREATE TABLE IF NOT EXISTS meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- Fictional demo participants, added once to a new database. They match the
-- built-in profiles in the sketch (DEMO_PROFILES).
INSERT OR IGNORE INTO participants (id, label)
  SELECT 101, 'Alex' WHERE NOT EXISTS (SELECT 1 FROM meta WHERE key = 'demo_seeded');
INSERT OR IGNORE INTO participants (id, label)
  SELECT 102, 'Jamie' WHERE NOT EXISTS (SELECT 1 FROM meta WHERE key = 'demo_seeded');
INSERT OR IGNORE INTO participants (id, label)
  SELECT 103, 'Taylor' WHERE NOT EXISTS (SELECT 1 FROM meta WHERE key = 'demo_seeded');
INSERT OR IGNORE INTO assignments (participant_id, exercise_id)
  SELECT 101, 7 WHERE NOT EXISTS (SELECT 1 FROM meta WHERE key = 'demo_seeded');
INSERT OR IGNORE INTO assignments (participant_id, exercise_id)
  SELECT 102, 9 WHERE NOT EXISTS (SELECT 1 FROM meta WHERE key = 'demo_seeded');
INSERT OR IGNORE INTO assignments (participant_id, exercise_id)
  SELECT 103, 12 WHERE NOT EXISTS (SELECT 1 FROM meta WHERE key = 'demo_seeded');
INSERT OR IGNORE INTO meta (key, value) VALUES ('demo_seeded', '1');
