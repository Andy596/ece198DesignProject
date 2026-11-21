# ece198DesignProject

## Portable Session Log Station for Personalized Care

An Arduino-based prototype for recording exercise sessions and transferring those records to a laptop dashboard. Developed for ECE 198 at the University of Waterloo.

**Project status — In development.** The latest local dual-display sketch, `ece198Codes1.0.1.ino`, supports participant profiles, a built-in exercise catalog, and editable technique scores. The repository's `main` branch contains the USB transfer and CSV export demo. Connecting the dual-display interface to USB transfer and the participant dashboard is future work.

## Problem

This project addresses **Sub Problem 1 — Scale Personalized Care** at the Centre for Community, Clinical and Applied Research Excellence (CCCARE).

Each participant receives a personalized exercise plan, called a dosage, that specifies the type and amount of activity suited to their needs. Health professionals review exercise history and adjust the plan as the participant progresses.

The challenge brief describes records spread across paper exercise logs, Airtable appointments, and a Microsoft Access database containing manually entered assessments and referrals. This creates extra work when staff need to review a participant's history or share updated information.

Our team is focusing on connecting exercise-session records to each participant's prescribed dosage so staff can review progress and keep plans up to date.

## Proposed Solution

The Session Log Station is a portable device instructors can carry while supervising exercise sessions. The Arduino interface lets them select a participant, assign exercises, and record technique scores. The full system is intended to transfer session entries over USB so staff can review exercise history, update plans, and export records. Health professionals remain responsible for exercise dosage decisions.

## Using the Dual-Display Prototype

The **OLED** shows the participant list, exercise menus, and keyboard. The **Grove LCD** shows scores and button prompts. The live bar at the top right of the OLED shows the potentiometer position on every screen. The centre marks identify the dead zone, which pauses scrolling and score adjustment.

- **Turn left or right** to scroll or adjust a score. Turning farther from the centre speeds up the changes.
- **Enter (A0)** selects or saves when the knob is in the dead zone.
- **Return (A1)** goes back one screen and discards unsaved edits. It works at any knob position.

### Add a Participant

1. Scroll to **+ Add user** and press Enter.
2. Enter a name of up to 12 characters. The keyboard automatically capitalizes the first letter of each word.
3. Enter an optional first exercise of up to five characters, or skip it to record a general **Technique score**.
4. Enter the initial score from 0 to 100 and save. The keyboard uses `>` to advance or save, `<` to delete, and `^` to cancel.

The prototype holds up to 10 profiles, including three sample profiles.

### Assign an Exercise and Edit Its Score

1. Open the participant's profile and select **+ Add exercise** at the bottom.
2. Browse the 55-exercise catalog and press Enter to add an exercise. The LCD shows **Already added** if that profile already has it.
3. Highlight an exercise in the profile and press Enter. The OLED shows the participant and exercise name while the LCD shows the score.
4. Turn the knob left to lower the score or right to raise it within the 0–100 range. An exercise with no saved score opens at 0.
5. Centre the knob and press Enter to save the score, or press Return to discard the edit.

Each profile holds its own assignments and latest scores. **The dual-display sketch stores these changes in RAM.** Resetting the Arduino or switching it off clears new profiles, assignments, and edits.

## Hardware

| Component | Connection or role |
| --- | --- |
| Arduino Uno R4 Minima | Runs the prototype and reads the controls. |
| 128 × 64 SSD1306 OLED | Shared I²C bus, address `0x3C`. Shows menus and the knob-position bar. |
| 16 × 2 Grove RGB LCD | Shared I²C bus. Shows scores and prompts. |
| Potentiometer | Analog input `A2`. Controls scrolling and score adjustment. |
| Grove Enter button | `A0`, active HIGH. Selects or saves. |
| Grove Return button | `A1`, active HIGH. Goes back or cancels. |
| Breadboard and jumper wires | Connect the prototype circuit. |
| USB cable and laptop | Program the Arduino and run the transfer and export demo. |

Portable power, the enclosure, and hardware transfer indicators are part of the wider prototype design.

## Software

The dual-display sketch uses `Wire`, the Grove RGB LCD library, Adafruit GFX, and Adafruit SSD1306. In the Arduino IDE, select **Arduino Uno R4 Minima**. To build an archived version, put a copy in its own sketch folder and give the folder and `.ino` file the same name, for example `ece198Codes101/ece198Codes101.ino`. The IDE compiles all `.ino` files in a sketch folder together.

The repository's transfer demo receives session entries over USB and stores them in SQLite for CSV export. A participant dashboard for viewing plans and exercise history is planned. The project concept includes sharing exported files through uWaterloo OneDrive.

## Transfer and Export (Noah)

The procedures below apply to the USB transfer demo on the `main` branch. [SessionLogStation demo](https://github.com/Andy596/ece198DesignProject/blob/main/SessionLogStation/SessionLogStation.ino)

### Repository layout

| Path | Contents |
| --- | --- |
| `SessionLogStation/` | Arduino sketch. Open `SessionLogStation.ino` in the Arduino IDE. `session_log.h/.cpp` hold on-device storage and the USB protocol. |
| `laptop/` | `sync.bash`, `export.bash`, and double-clickable macOS launchers (`Sync Device.command`, `Export CSV.command`). The SQLite database `session_log.db` is created here. |
| `sim/` | Host simulator that runs the real sketch, plus tests (`make test`). |

### Hooks for integrating the input code

The transfer demo provides these functions.

- `confirmEntry(participantId, exerciseId, value, technique)` stores a session entry and shows the result on the LCD.
- `undoLastEntry()` removes the newest entry if it has not been synced yet.
- Lower-level calls in `session_log.h` include `slog::entryCount()`, `slog::getEntry()`, `slog::unsyncedCount()`, `slog::safeToPowerOff()`, and `slog::takeEvent()`.

When integrating the two sketches, map participant profiles to database IDs and the OLED exercise catalog to database exercise IDs. The demo's `kExerciseNames` and `laptop/schema.sql` currently use matching IDs for five exercises.

### Syncing and exporting

1. Plug the device into the laptop by USB.
2. Double-click `laptop/Sync Device.command` on macOS, or run `./laptop/sync.bash` on macOS or Linux.
3. Wait for "Safe to switch off" before powering the device down.
4. Double-click `Export CSV.command`, or run `./laptop/export.bash [--participant ID]`. CSV files are written to `laptop/exports/`.

Requirements: `bash` and `sqlite3`, both preinstalled on macOS. Windows would need WSL or Git Bash and has not been tested.

### Protocol summary

The protocol is line-based text at 115200 baud. Every line the device sends ends in `*HH`, a CRC-8 checksum (polynomial 0x07). The laptop sends `SYNC`, and the device replies with `BEGIN <boot> <n>`, then `n` lines of `E <boot> <seq> <participant> <exercise> <value> <technique>`, then `END`. The laptop commits all entries to SQLite in one transaction before it replies `ACK <boot> <lastSeq>*HH`. The device then marks the entries as synced and answers `DONE <boot> <lastSeq> <remaining>`. `(boot, seq)` is unique in the database, so re-sending entries never creates duplicates. `boot` is a power-up counter stored in EEPROM.

## Team

| Member | Main Responsibilities |
| --- | --- |
| Andy | Hardware and software for device inputs. |
| Noah | Data transfer between the Arduino and laptop, database synchronization, and exports. |
| Wenjun | Software interface and input handling. |
| Caleb | Enclosure design and circuit assembly. |

## Prototype Validation

The dual-display sketch compiled for the Arduino Uno R4 Minima. Simulated tests checked both participant creation paths, exercise assignments, score editing and cancellation, Return navigation, and the OLED knob bar.

Planned end-to-end checks include verifying that entries go to the selected participant, stay available during a session, and transfer accurately to the laptop before shutdown. The device should show a successful transfer only after the receiving application confirms it. Repeated transfers should preserve existing records without creating duplicates. Exported files should match the saved session data.

Development and demonstrations should use fictional participant data.

## Project Resources

- [IDEAs Clinic — Scalable Systems for Personalized Care](https://github.com/IdeasClinicUWaterloo/W26_HealthTech_Innovation_Challenge_Guides/tree/main/Scalable_Systems_for_Personalized_Care)
- [IDEAs Clinic — SQL Database Guide](https://github.com/IdeasClinicUWaterloo/W26_HealthTech_Innovation_Challenge_Guides/blob/main/Scalable_Systems_for_Personalized_Care/SQL_Database_Guide.md)
- [IDEAs Clinic — Tech Resources](https://github.com/IdeasClinicUWaterloo/TechResources)
- [IDEAs Clinic — Sensors and Displays](https://github.com/IdeasClinicUWaterloo/TechResources/tree/main/Microcontrollers/Sensors)
- *Dosages and Exercise Sessions*, provided through the course LEARN site.
