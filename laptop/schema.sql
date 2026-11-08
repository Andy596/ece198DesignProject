-- Session Log Station database (SQLite). Safe to run on every start.

CREATE TABLE IF NOT EXISTS participants (
  id    INTEGER PRIMARY KEY,   -- the ID selected on the device
  label TEXT NOT NULL          -- e.g. a clinic code; use fictional data for demos
);

-- id must match the index in kExerciseNames in SessionLogStation.ino.
CREATE TABLE IF NOT EXISTS exercises (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL
);

INSERT OR IGNORE INTO exercises (id, name) VALUES
  (0, 'Squat'), (1, 'Lunge'), (2, 'Push-up'), (3, 'Plank'), (4, 'Row');

-- No foreign keys on purpose: an entry for an unknown participant must still
-- be saved, or the device would never get its ACK. Exports flag unknown IDs.
CREATE TABLE IF NOT EXISTS entries (
  id             INTEGER PRIMARY KEY,
  boot           INTEGER NOT NULL,  -- device power-up counter
  seq            INTEGER NOT NULL,  -- entry number within that boot
  participant_id INTEGER NOT NULL,
  exercise_id    INTEGER NOT NULL,
  value          INTEGER NOT NULL,
  technique      INTEGER NOT NULL,
  received_at    TEXT    NOT NULL,  -- laptop local time; the device has no clock
  UNIQUE (boot, seq)                -- re-syncing the same entry is a no-op
);
