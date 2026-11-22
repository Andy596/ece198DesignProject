# ece198DesignProject

## Portable Session Log Station for Personalized Care

An Arduino prototype for recording prescribed exercise dosages and what participants complete at CCCARE. Developed for ECE 198 at the University of Waterloo.

**Firmware version** 1.0.7

Open `ece198Codes107/ece198Codes107.ino` in Arduino IDE. Run the included `sync_dosages.py` to save USB snapshots as CSV.

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

The sketch uses `Wire`, the Grove RGB LCD library, Adafruit GFX, and Adafruit SSD1306. In Arduino IDE, select **Arduino Uno R4 Minima**. Open one version in its own matching sketch folder, for example `ece198Codes107/ece198Codes107.ino`. Arduino IDE compiles all `.ino` files in a sketch folder together.

## Sync to the Laptop

1. Connect the Arduino by USB and close Arduino Serial Monitor.
2. From the folder containing the receiver, run `python3 sync_dosages.py` on macOS or Linux. Wait for the listening prompt. If several boards are connected, pass `--port DEVICE`.
3. Open the main user menu and tap A3. The Arduino shows **USB sync done** after the receiver saves and acknowledges the CSV.
4. Open the timestamped CSV in the folder where you ran the receiver. It contains current exercise plans and completed entries. Completed rows include the session number, the saved prescription, and the actual values.

The prototype stores profiles, prescriptions, and up to **64 completed exercise entries in RAM**. Sync before resetting or powering off to retain a CSV copy on the laptop.

## USB Data for Database Integration

The current receiver saves a **full snapshot** on each successful sync. A `plan` row describes a current assignment. A `completed` row contains the session number, the prescription saved with the entry, and the actual values. Dosage columns include the number of sets, the amount per set, the measure (`reps` or `sec`), and the load and unit (`kg` or `lb`).

- `participant_id` and `session` are assigned by the firmware and restart after a reset. Agree on a persistent identity scheme before using them as database keys across boots.
- Exercise labels are sent as text. Map them to database exercise IDs, and define how repeated snapshots and corrections to previously exported entries should be imported.
- At 115200 baud, the firmware sends `DOSAGE_BEGIN <id> <row_count>`, the CSV header and rows, then `DOSAGE_END <id>`. The receiver sends `ACK <id>` after saving the file. This receiver uses the dosage snapshot format. The existing SQLite demo has a session-entry schema with a single exercise value and technique field. [Existing SQLite schema](https://github.com/Andy596/ece198DesignProject/blob/main/laptop/schema.sql) · [Existing transfer module](https://github.com/Andy596/ece198DesignProject/blob/main/SessionLogStation/session_log.h)

## Prototype Validation

The sketch compiles for the Arduino Uno R4 Minima. Simulated checks cover session navigation, participant isolation, and completed Squat values that change across sessions while the prescription stays the same. A serial loopback test verified CSV output and acknowledgement.

For the hardware handoff test, use fictional data. Log different completed Squat values in two sessions under the same prescription. Use history to check both sets of values, then tap A3 from the main menu and confirm the CSV contains both sessions with the correct values and units.

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
