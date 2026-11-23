# ece198DesignProject

## Portable Session Log Station for Personalized Care

An Arduino prototype for recording prescribed exercise dosages and what participants complete at CCCARE. Developed for ECE 198 at the University of Waterloo.

**Firmware version** 1.0.8

Open `ece198Codes108/ece198Codes108.ino` in Arduino IDE. To save records on the laptop, run `laptop/sync_dosages.py` (or double-click `laptop/Sync Device.command`) and tap A3 on the main menu. Records go into the SQLite database `laptop/session_log.db`.

1.0.8 is 1.0.7 plus a device identity for USB sync (see [USB Data and the Database](#usb-data-and-the-database)). `ece198Codes1.0.7.ino` is kept for reference; the laptop refuses its snapshots because they can't be told apart across resets.

## Problem

This project addresses **Sub Problem 1 — Scale Personalized Care** at the Centre for Community, Clinical and Applied Research Excellence (CCCARE).

Each participant receives a personalized exercise plan, called a dosage, that specifies the type and amount of activity suited to their needs. Health professionals review exercise history and adjust the plan as the participant progresses.

The challenge brief describes records spread across paper exercise logs, Airtable appointments, and a Microsoft Access database containing manually entered assessments and referrals. This creates extra work when staff need to review a participant's history or share updated information.

Our team is focusing on connecting exercise-session records to each participant's prescribed dosage so staff can review progress and keep plans up to date.

## Controls and Displays

The OLED shows four rows in the main user menu, plus menus, session history, and a live knob-position bar. The centre marks indicate the dead zone. The large Grove LCD shows dosage values, completed values, and button prompts.

| Control | Use |
| --- | --- |
| Potentiometer (A2) | Turn left or right to scroll or adjust a value. Centre it before pressing Enter. |
| Enter (A0) | Open the highlighted item, advance through a form, or save from the review screen. |
| Return (A1) | Go back one screen or discard the current draft. |
| Touch sensor (A3) | Save a name or prescribed dosage, review and save a completion, or start USB sync from the main user menu. |

## Set Up a Profile and Prescribe Exercises

1. From the main menu, select **+ Add user**. Enter a name with the on-screen keyboard and tap A3 or select the `>` key to save. The list is sorted alphabetically and supports up to 10 profiles.
2. Open the profile and select **+ Add exercise**. Choose from the 55-exercise catalog.
3. Open an assigned exercise and select **Edit dosage**. Enter sets, reps or seconds per set, and load in kg or lb. Use zero load for exercises with no added resistance.
4. Save the values. Health professionals remain responsible for selecting the dosage.

## Start a Session and Log Completed Work

1. In the participant’s profile, select **New session** at the start of a visit. Saving the first completed exercise also starts a session if the participant has none in progress.
2. In that profile, open the exercise and choose **Log completed**. Enter the sets, reps or seconds per set, and load the participant actually completed.
3. Press Enter through the fields or tap A3 after filling the required values to open the review screen. On that screen, press Enter or tap A3 to save.
4. Log the remaining exercises for that visit. They all use the participant’s current session number automatically. The sketch stores one completed entry per exercise per session. Reopening **Log completed** loads that entry for editing. To record a second set with the same reps and load, change **Sets** from 1 to 2 and save.

Each entry stores one reps-or-seconds value and one load for all sets. It also keeps a copy of the prescription at the time it was first logged. Later changes to the exercise plan leave that copy intact.

## Browse Past Sessions

1. Open a participant’s profile and select **Session history**. It opens on the newest session with a saved entry.
2. Turn the knob **left for older sessions** or **right for newer sessions**. The OLED lists the participant’s saved session numbers. A session appears after at least one exercise has been logged.
3. Press Enter on a session number to see the exercises logged in that session. Scroll through the exercises and press Enter to open one.
4. The OLED shows the session number, **Done** and **Done load** for the completion, then **Plan** and **Plan load** for the original prescription. The large LCD also shows the completed values. Turn the knob to compare the other exercises in that same session. Use Return to go back to the exercise list and then the session list.

To review the same exercise across visits, go to **Participant → Exercise → View history**. The knob moves between that exercise’s earlier and later entries.

## Hardware and Arduino Setup

| Component | Connection or role |
| --- | --- |
| Arduino Uno R4 Minima | Runs the sketch. |
| 128 × 64 SSD1306 OLED | I²C address `0x3C`. Shows menus, the knob bar, and both completed and prescribed values in history. |
| 16 × 2 Grove RGB LCD | Shared I²C bus. Displays values, status messages, and completed work. |
| Potentiometer | Analog input `A2`. |
| Grove Enter and Return buttons | `A0` and `A1`, active HIGH. |
| Grove digital touch sensor | `A3`, active HIGH. |
| Laptop and USB cable | Program the board and receive CSV snapshots. |

The sketch uses `Wire`, the Grove RGB LCD library, Adafruit GFX, and Adafruit SSD1306. In Arduino IDE, select **Arduino Uno R4 Minima**. Open one version in its own matching sketch folder, for example `ece198Codes108/ece198Codes108.ino`. Arduino IDE compiles all `.ino` files in a sketch folder together.

## Sync to the Laptop

Requirements: macOS or Linux with `python3` (standard library only). Windows has not been tested.

1. Connect the Arduino by USB and close Arduino Serial Monitor.
2. Double-click `laptop/Sync Device.command`, or run `python3 laptop/sync_dosages.py`. Wait for the listening prompt. If several boards are connected, pass `--port DEVICE`.
3. Open the main user menu and tap A3. The Arduino shows **USB sync done** only after the laptop has committed the records to its database. **Laptop save fail** or **No save reply** means nothing was saved; the Arduino still holds every record, so sync again.
4. To get spreadsheets, double-click `laptop/Export CSV.command` or run `python3 laptop/export_dosages.py`. It writes `completed_<time>.csv` (one row per exercise per session, with the prescription saved at the time and the actual values) and `plans_<time>.csv` (current prescriptions) to `laptop/exports/`.

Syncing as often as you like is safe: records already on the laptop are never duplicated. The prototype stores profiles, prescriptions, and up to **64 completed exercise entries in RAM**, so sync before resetting or powering off.

## USB Data and the Database

### Wire format (115200 baud)

The firmware sends a full snapshot, then waits up to 10 s for the answer.

```
DOSAGE_BEGIN <transfer_id> <row_count> <device_id> <boot>
record,participant_id,participant,exercise,session,planned_sets,planned_amount,planned_measure,planned_load,planned_unit,actual_sets,actual_amount,actual_measure,actual_load,actual_unit
<row_count CSV rows>
DOSAGE_END <transfer_id>
```

The laptop answers `ACK <transfer_id>` after the database commit, or `ERR <transfer_id>` if anything failed. A `plan` row is a current assignment; a `completed` row is one exercise in one session with the prescription copied at the first save and the actual values. Measures are `reps` or `sec`, load units `kg` or `lb`, and loads go in steps of 0.25.

### Identity across resets

`participant_id`, `session` and `transfer_id` restart after a reset, so the same numbers mean different records in different power-ups. 1.0.8 adds two values kept in EEPROM:

- `device_id`: 8 hex digits, random, created at the board's first sync (from the microsecond timing of the A3 tap and knob noise).
- `boot`: counts power-ups.

The laptop keys every completed entry by **(device_id, boot, participant_id, session, exercise)**. Within one power-up the firmware never creates two entries with the same participant, session and exercise; saving again edits the entry. As a safety net, the laptop refuses a transfer ID that doesn't increase within a boot, which would mean the identity was reused.

The device has no persistent profiles, so after a reset the same person gets a new participant record. Exports include names so staff can match people across power-ups. Linking them automatically would need persistent IDs on the device.

### Import rules (`laptop/dosage_db.py`)

- Every snapshot is fully validated, then saved in one SQLite transaction. Any error saves nothing and answers `ERR`.
- A completed entry seen again with the same values is left alone. Different values are a correction: the entry is updated, its `revision` goes up, and the old values are kept in `dosage_entry_revisions`.
- Entries missing from a later snapshot are kept. Plans mirror the latest snapshot of each power-up, and participants deleted on the device are marked `on_device = 0`.
- The raw snapshot is stored in `dosage_transfers` for auditing.
- Tables are prefixed `dosage_` and live in the same `session_log.db` as the original transfer demo's `entries` table. That table holds one value and one technique score per entry, which can't hold planned and actual dosages with units, so the dosage records have their own tables.

### Original transfer demo

`SessionLogStation/` and `laptop/sync.bash`, `export.bash` and `schema.sql` are the earlier single-value USB demo (CRC-checked lines, `SYNC`/`ACK`). They still work and are tested, but firmware 1.0.8 uses the dosage format above.

## Testing Without the Board

`cd sim && make test` builds the real sketches against host stand-ins for the Arduino libraries, with address and undefined-behaviour sanitizers, and runs:

- `test_dosage_end_to_end.py`: the 1.0.8 sketch, driven through its buttons, knob and A3, talks to the real `sync_dosages.py` over a pseudo-terminal. It covers two Squat sessions with the same prescription, repeated syncs, a correction, and a failed database write (records kept, retry succeeds). It also covers a locked database, a lost row, power cycles, no receiver running, a stalled USB link, erased EEPROM, and 1.0.7 being refused.
- `laptop/test_dosage_db.py`: validation and import rules, including malformed rows, corrections, resets, concurrent receivers and the largest possible snapshot.
- `test_session_log` and `test_end_to_end.bash`: the original transfer demo.

Requirements: `g++`, `python3`, and `sqlite3` (for the original demo's tests).

## Prototype Validation

1.0.7 compiles for the Arduino Uno R4 Minima. Simulated checks cover session navigation, participant isolation, and completed Squat values that change across sessions while the prescription stays the same. A serial loopback test verified CSV output and acknowledgement. 1.0.8 and the database receiver pass the host tests above. 1.0.8 has not yet been compiled with the Renesas core or run on the board.

For the hardware handoff test, use fictional data:

1. Log different completed Squat values in two sessions under the same prescription. Use history to check both sets of values.
2. Run the receiver and tap A3 on the main menu. Export and confirm both sessions appear with the correct planned and actual values and units.
3. Sync again and confirm the receiver reports `0 new, 0 corrected`.
4. Correct one entry with **Log completed**, sync, and confirm `1 corrected` and that only that session changed.
5. Simulate a failed laptop save: in a second terminal run `sqlite3 laptop/session_log.db` and type `BEGIN EXCLUSIVE;`. Run the receiver, tap A3, and confirm the Arduino shows **Laptop save fail** and nothing new was saved. Type `ROLLBACK;` in the second terminal, sync again, and confirm the records arrive.
6. Power-cycle the board, log Squat for session 1 again, sync, and confirm the earlier power-up's records are unchanged.

## Team

| Member | Main Responsibilities |
| --- | --- |
| Andy | Hardware and software for device inputs. |
| Noah | Data transfer between the Arduino and laptop, database synchronization, and exports. |
| Wenjun | Software interface and input handling. |
| Caleb | Enclosure design and circuit assembly. |

## Project Resources

- [IDEAs Clinic — Scalable Systems for Personalized Care](https://github.com/IdeasClinicUWaterloo/W26_HealthTech_Innovation_Challenge_Guides/tree/main/Scalable_Systems_for_Personalized_Care)
- [IDEAs Clinic — SQL Database Guide](https://github.com/IdeasClinicUWaterloo/W26_HealthTech_Innovation_Challenge_Guides/blob/main/Scalable_Systems_for_Personalized_Care/SQL_Database_Guide.md)
- [IDEAs Clinic — Tech Resources](https://github.com/IdeasClinicUWaterloo/TechResources)
- [IDEAs Clinic — Sensors and Displays](https://github.com/IdeasClinicUWaterloo/TechResources/tree/main/Microcontrollers/Sensors)
- *Dosages and Exercise Sessions*, provided through the course LEARN site.
