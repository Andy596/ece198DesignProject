// Session Log Station - main sketch (Arduino R4 Minima + Grove RGB LCD).
//
// Display/knob code: Andy. Storage + USB transfer (session_log.*): Noah.
//
// HOW TO HOOK UP THE INPUT CODE
//   When the instructor confirms an entry, call:
//       confirmEntry(participantId, exerciseId, value, techniqueScore);
//   It stores the entry and shows the result on the LCD. For "undo", call
//   undoLastEntry(). Everything else (USB sync, status messages) is handled
//   in loop() by serviceSessionLog().
#include <Wire.h>
#include <EEPROM.h>
#include "rgb_lcd.h"
#include "session_log.h"

// Set to 0 once real input code creates entries.
#ifndef SLOG_DEMO_SEED
#define SLOG_DEMO_SEED 1
#endif

rgb_lcd lcd;

// --- Display lookup tables -------------------------------------------------
// exerciseId is an index into this list. laptop/schema.sql holds the same
// list; keep the two in the same order.
const char* const kExerciseNames[] = {"Squat", "Lunge", "Push-up", "Plank", "Row"};
const uint8_t kExerciseCount = sizeof kExerciseNames / sizeof kExerciseNames[0];

const int kKnobPin = A2;
const uint32_t kRefreshMs = 80;
const uint32_t kMessageMs = 2000;   // how long status messages stay up
const int kBootCounterAddr = 0;     // EEPROM address of the uint32_t boot counter
const uint8_t kMaxBytesPerLoop = 64; // bound on serial bytes handled per loop()

int displayedIndex = -2;           // -2 forces a first draw
uint16_t displayedCount = 0xFFFF;
uint32_t lastRefreshMs = 0;
uint32_t messageStartMs = 0;
bool messageActive = false;

// --- Session log glue ------------------------------------------------------
void writeToUsb(const char* text, size_t len) {
  Serial.write(reinterpret_cast<const uint8_t*>(text), len);
}

// Increments a counter in EEPROM so every power-up gets a new boot ID.
uint32_t nextBootId() {
  uint32_t boot = 0;
  EEPROM.get(kBootCounterAddr, boot);  // erased EEPROM reads 0xFFFFFFFF -> wraps to 0
  ++boot;
  EEPROM.put(kBootCounterAddr, boot);
  return boot;
}

void showMessage(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  messageStartMs = millis();
  messageActive = true;
  displayedIndex = -2;  // redraw the entry view when the message expires
}

// Hook for the input code. Returns true if the entry was stored.
bool confirmEntry(uint16_t participantId, uint8_t exerciseId, int16_t value, uint8_t technique) {
  if (exerciseId >= kExerciseCount) {
    showMessage("Unknown exercise", "Not saved");
    return false;
  }
  switch (slog::addEntry(participantId, exerciseId, value, technique)) {
    case slog::Status::Ok:
      showMessage("Entry saved", "");
      return true;
    case slog::Status::Full:
      lcd.setRGB(255, 0, 0);
      showMessage("Device full!", "Sync to laptop");
      return false;
    case slog::Status::Invalid:
      showMessage("Invalid entry", "Not saved");
      return false;
    default:
      showMessage("Not saved", "");
      return false;
  }
}

// Hook for an "undo" button.
bool undoLastEntry() {
  const slog::Status s = slog::removeLastEntry();
  if (s == slog::Status::Ok) {
    showMessage("Last entry", "removed");
    return true;
  }
  showMessage("Can't undo:", s == slog::Status::Empty ? "no entries" : "already synced");
  return false;
}

void serviceSessionLog() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < kMaxBytesPerLoop && Serial.available() > 0; ++i) {
    const int c = Serial.read();
    if (c >= 0) {
      slog::feed(static_cast<char>(c), now);
    }
  }
  slog::poll(now);

  char line2[17];
  switch (slog::takeEvent()) {
    case slog::Event::SyncStarted:
      lcd.setRGB(0, 0, 255);
      showMessage("Syncing...", "");
      break;
    case slog::Event::SyncConfirmed:
      lcd.setRGB(0, 255, 0);
      snprintf(line2, sizeof line2, "%u saved", static_cast<unsigned>(slog::lastSyncCount()));
      showMessage("Sync complete", line2);
      break;
    case slog::Event::SyncTimedOut:
      lcd.setRGB(255, 128, 0);
      showMessage("Sync failed", "Try again");
      break;
    case slog::Event::ProtocolError:
    case slog::Event::None:
    default:
      break;
  }
}

// --- Andy's entry view (knob scrolls through entries on the device) ---------
void drawEntryView() {
  const uint16_t count = slog::entryCount();
  if (count == 0) {
    if (displayedIndex != -1) {
      lcd.clear();
      lcd.print("No entries");
      displayedIndex = -1;
    }
    return;
  }
  const int knob = analogRead(kKnobPin);  // 0..1023 (10-bit default)
  const int selected = static_cast<int>(static_cast<long>(knob) * count / 1024L);
  if (selected == displayedIndex && count == displayedCount) {
    return;
  }
  slog::Entry e;
  if (slog::getEntry(static_cast<uint16_t>(selected), e) != slog::Status::Ok) {
    return;
  }
  char line1[17];
  char line2[17];
  const char* ex = (e.exerciseId < kExerciseCount) ? kExerciseNames[e.exerciseId] : "?";
  snprintf(line1, sizeof line1, "P%u %s", static_cast<unsigned>(e.participantId),
           e.synced ? "synced" : "");
  snprintf(line2, sizeof line2, "%s %d T%u", ex, static_cast<int>(e.value),
           static_cast<unsigned>(e.technique));
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  displayedIndex = selected;
  displayedCount = count;
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);
  slog::begin(nextBootId(), writeToUsb);

#if SLOG_DEMO_SEED
  // Fictional demo data (participant IDs, not names).
  confirmEntry(101, 0, 12, 86);  // Squat x12, technique 86
  confirmEntry(102, 1, 10, 92);  // Lunge x10
  confirmEntry(103, 2, 8, 78);   // Push-up x8
#endif
}

void loop() {
  serviceSessionLog();

  const uint32_t now = millis();
  if (now - lastRefreshMs < kRefreshMs) {
    return;
  }
  lastRefreshMs = now;

  // Keep a status message up for a moment, then go back to the entry view.
  // (Unsigned subtraction stays correct when millis() wraps.)
  if (messageActive) {
    if (now - messageStartMs < kMessageMs) {
      return;
    }
    messageActive = false;
    if (!slog::awaitingAck()) {
      lcd.setRGB(255, 255, 255);
    }
  }
  drawEntryView();
}
