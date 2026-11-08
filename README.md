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

## Arduino Libraries

Install the following libraries through the Arduino IDE Library Manager. The versions listed match the team's installed setup.

| Library | Installed Version | Purpose |
| --- | --- | --- |
| Adafruit BusIO | 1.17.4 | Communication support for Adafruit libraries using I2C, SPI, and UART. |
| Adafruit GFX Library | 1.13.0 | Drawing text and graphics on the OLED display. |
| Adafruit SSD1306 | 2.5.17 | Driver for the SSD1306 OLED display. |
| Grove - LCD RGB Backlight | 1.0.2 | Control of the Grove LCD display and its RGB backlight. |

The OLED option uses the Adafruit libraries. The Grove LCD option uses the Grove - LCD RGB Backlight library.

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
