# ece198DesignProject

## Portable Session Log Station for Personalized Care

An Arduino-based prototype for recording exercise sessions and transferring those records to a laptop dashboard. Developed for ECE 198 at the University of Waterloo.

**Project status — In development.** The concept is a portable Session Log Station. The features below describe the intended prototype. The display, database, dashboard software, and export format are to be determined.

## Problem

This project addresses **Sub Problem 1 — Scale Personalized Care** at the Centre for Community, Clinical and Applied Research Excellence (CCCARE).

Each participant receives a personalized exercise plan, called a dosage, that specifies the type and amount of activity suited to their needs. Health professionals review exercise history and adjust the plan as the participant progresses.

The challenge brief describes records spread across paper exercise logs, Airtable appointments, and a Microsoft Access database containing manually entered assessments and referrals. This creates extra work when staff need to review a participant's history or share updated information.

Our team is focusing on connecting exercise-session records to each participant's prescribed dosage so staff can review progress and keep plans up to date.

## Proposed Solution

The Session Log Station is a portable device that instructors can carry while supervising exercise sessions. Physical controls and a display let instructors select a participant and record exercise information. Entries are held temporarily on the device during a session, then transferred to a laptop through USB.

**Session data must be synced to the laptop or another compatible device before the Arduino is switched off.**

The laptop application is intended to organize the records by participant, display session history, and support exporting and sharing information. Health professionals remain responsible for exercise dosage decisions.

## Intended Workflow

1. Select a participant using their assigned ID.
2. Select the exercise and enter session information using the device's controls.
3. Review the entry on the display and confirm it.
4. Keep confirmed entries on the device while it is being used away from the laptop.
5. Connect the device to the laptop through USB and transfer the entries.
6. Confirm that the transfer succeeded before switching off the Arduino.
7. Review the records on the dashboard and export them for further use.

## Planned Features

| Feature | Purpose |
| --- | --- |
| Participant selection | Associate each session entry with the correct participant. |
| Exercise and technique logging | Record exercise values and instructor-entered technique scores. |
| Display and physical controls | Allow instructors to review and confirm entries on the device. |
| Temporary session storage | Hold entries during use until they can be transferred before shutdown. |
| USB transfer | Move recorded entries into the laptop's database. |
| Participant dashboard | Make exercise plans and session history easier to review and update. |
| Data export | Support downloading records for spreadsheet use and sharing. |

## Hardware

| Component | Intended Role |
| --- | --- |
| Arduino R4 Minima | Process inputs, manage session entries, and communicate with the laptop. |
| OLED or Grove LCD display | Show participant selection, exercise values, and confirmation messages. Final selection is pending. |
| Potentiometer | Scroll through participants and adjust input values. |
| Three pushbuttons | Select options, confirm entries, and enter technique scores. |
| Two LEDs with suitable resistors | Indicate input feedback and transfer status. |
| Breadboard and jumper wires | Connect the prototype circuit. |
| Portable power source | Power the device while it is carried between participants. |
| Enclosure | Hold the controls, display, and circuitry. |
| USB cable and laptop | Transfer records and run the dashboard. |

The selected display, its required wiring, and the portable power arrangement will be documented with the final circuit.

## Software

The planned system has three main parts.

- **Arduino firmware** handles navigation, data entry, display feedback, temporary session storage, and USB communication.
- **Laptop transfer software** receives entries and saves them to the participant database.
- **Dashboard software** presents participant records and supports updates and exports.

The database, dashboard framework, and export format are being selected. The project concept includes sharing exported files through uWaterloo OneDrive.

## Transfer and Export (Noah)

### Repository layout

| Path | Contents |
| --- | --- |
| `SessionLogStation/` | Arduino sketch. Open `SessionLogStation.ino` in the Arduino IDE. `session_log.h/.cpp` hold on-device storage and the USB protocol. |
| `laptop/` | `sync.bash`, `export.bash`, and double-clickable macOS launchers (`Sync Device.command`, `Export CSV.command`). The SQLite database `session_log.db` is created here. |
| `sim/` | Host simulator that runs the real sketch, plus tests (`make test`). |

### Hooks for the input code

Call these from the button/knob code in `SessionLogStation.ino`:

- `confirmEntry(participantId, exerciseId, value, technique)` stores an entry and shows the result on the LCD.
- `undoLastEntry()` removes the newest entry if it has not been synced yet.
- Lower-level calls in `session_log.h`: `slog::entryCount()`, `slog::getEntry()`, `slog::unsyncedCount()`, `slog::safeToPowerOff()`, `slog::takeEvent()`.

Exercise IDs are indexes into `kExerciseNames` in the sketch and must match the `exercises` table in `laptop/schema.sql`.

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

The planned checks are to verify that entries are linked to the selected participant, stay available during a session, and transfer accurately to the laptop before shutdown. The device should show a successful transfer only after the receiving application confirms it. Repeated transfers should preserve existing records without creating duplicates. Exported files should match the saved session data.

Development and demonstrations should use fictional participant data.

## Project Resources

- [IDEAs Clinic — Scalable Systems for Personalized Care](https://github.com/IdeasClinicUWaterloo/W26_HealthTech_Innovation_Challenge_Guides/tree/main/Scalable_Systems_for_Personalized_Care)
- [IDEAs Clinic — SQL Database Guide](https://github.com/IdeasClinicUWaterloo/W26_HealthTech_Innovation_Challenge_Guides/blob/main/Scalable_Systems_for_Personalized_Care/SQL_Database_Guide.md)
- [IDEAs Clinic — Tech Resources](https://github.com/IdeasClinicUWaterloo/TechResources)
- [IDEAs Clinic — Sensors and Displays](https://github.com/IdeasClinicUWaterloo/TechResources/tree/main/Microcontrollers/Sensors)
- *Dosages and Exercise Sessions*, provided through the course LEARN site.
