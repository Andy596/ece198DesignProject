// CCCARE Session Log Station, version 1.0.2.
//
// Merges the dual-display profile/score interface (1.0.1, Andy and Wenjun)
// with USB transfer to the laptop database (session_log, Noah).
//
// - Participant profiles (ID, label, assigned exercises, latest scores) come
//   from the laptop. Run laptop/sync.bash (or double-click "Sync Device") to
//   upload saved scores and download the current profiles in one step.
// - Every time a score is saved, one entry (participant ID, exercise ID,
//   score) is logged for the next sync. Unsaved edits are never logged.
//
// Controls: turn the knob to scroll or adjust, centre it and press Enter (A0)
// to select or save. Return (A1) goes back and discards unsaved edits.
//
// Folder layout: this file must stay in a folder of the same name, next to
// session_log.* and profile_store.* (the Arduino IDE compiles them together).
#include <Wire.h>
#include <EEPROM.h>
#include "rgb_lcd.h"
#include <Adafruit_GFX.h>
// Avoid the legacy color macros that conflict with the Grove LCD library.
#define NO_ADAFRUIT_SSD1306_COLOR_COMPATIBILITY
#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <string.h>

#include "session_log.h"
#include "profile_store.h"

// Built-in fictional profiles so the device is usable before its first sync.
// They match the demo participants laptop/schema.sql creates. The first
// sync replaces them with the laptop's list.
#ifndef DEMO_PROFILES
#define DEMO_PROFILES 1
#endif

rgb_lcd lcd;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// Exercise IDs are indexes into this list. laptop/schema.sql holds the same
// list in the same order (sim/test_ui.cpp checks that they match).
const char* const exerciseCatalog[] = {
  // Mobility
  "Arm rainbows", "Standing hip opener/closer", "Airplane",
  "Thoracic spine rotations", "Wall angels", "Scapular squeezes",

  // Squat / Lunge
  "Ball squats", "Goblet squats", "Step-ups", "Lunges",
  "Wall sits", "Box squats",

  // Push
  "Push-ups", "Chest press", "Chest fly", "Incline press",
  "Cable crossovers", "Wall push-ups", "Shoulder press",
  "Arnold press", "Front raises", "Close-grip push-ups",

  // Hinge
  "Romanian deadlifts", "Good mornings", "Kettlebell swings",
  "Glute bridges", "Deadlifts", "Stability ball leg curls",

  // Pull
  "Bent-over row", "Seated row", "Lat pulldown",
  "Resistance band rows", "TRX row", "Reverse fly",
  "Bicep curls", "Hammer curls", "Cable curls",

  // Carry
  "Farmer's carry", "Suitcase carry",

  // Core
  "Plank", "Side plank", "Bird dog", "Dead bug", "Torso rotations",
  "Mountain climbers", "Leg raises", "Pallof press",

  // Stretches
  "Hamstring stretch", "Calf stretch", "Chest doorway stretch",
  "Upper trap stretch", "Child's pose", "Hip flexor stretch",
  "Cat-cow stretch", "Neck side stretch"
};

const int catalogCount = sizeof(exerciseCatalog) / sizeof(exerciseCatalog[0]);
static_assert(sizeof(exerciseCatalog) / sizeof(exerciseCatalog[0]) == pstore::kExerciseCount,
              "exerciseCatalog and pstore::kExerciseCount must agree");

const int maxScore = pstore::kMaxScore;
const int noScore = pstore::kNoScore;

enum Screen : uint8_t { UsersScreen, ProfileScreen, CatalogScreen, ScoreScreen };
Screen screen = UsersScreen;

int selectedUser = 0;
uint16_t selectedUserId = 0;  // kept so a profile download can reselect the same person
int selectedExerciseRow = 0;
int draftScore = 0;  // The stored score changes only when the user presses Save.

// The knob stops scrolling between these two thresholds.
const int leftThreshold = 460;
const int rightThreshold = 563;
const int knobMax = 1023;
const unsigned long slowScrollInterval = 600;
const unsigned long fastScrollInterval = 120;

// Reserve the top-right 40 pixels of each OLED header for the knob bar.
const int knobBarX = 88;
const int knobBarWidth = 40;
const int knobTrackLeft = knobBarX + 2;
const int knobTrackRight = knobBarX + knobBarWidth - 3;
const int deadZoneLeftX = knobBarX + 17;
const int deadZoneRightX = knobBarX + 22;

int knobPosition = knobMax / 2;
int displayedKnobX = -1;

int scrollDirection = 0;
unsigned long lastScrollTime = 0;

// Status messages (save results, sync progress) use the LCD for a moment.
const unsigned long statusTime = 1800;
bool statusActive = false;
unsigned long statusStart = 0;

const int bootCounterAddr = 0;  // EEPROM address of the uint32_t boot counter
const uint8_t maxSerialBytesPerLoop = 64;
bool oledOk = true;

// Both Grove buttons are active HIGH and debounce independently.
const unsigned long debounceTime = 35;

struct GroveButton {
  explicit GroveButton(int inputPin) : pin(inputPin) {}

  void begin() {
    pinMode(pin, INPUT);
    lastReading = digitalRead(pin);
    stableReading = lastReading;
  }

  // Report one press, even if the button stays held.
  bool pressed(unsigned long now) {
    int reading = digitalRead(pin);
    if (reading != lastReading) {
      lastReading = reading;
      lastChange = now;
    }

    if (reading != stableReading && now - lastChange >= debounceTime) {
      stableReading = reading;
      return stableReading == HIGH;
    }
    return false;
  }

  int pin;
  int lastReading = LOW;
  int stableReading = LOW;
  unsigned long lastChange = 0;
};

GroveButton enterButton(A0);
GroveButton returnButton(A1);

// --- USB and storage glue -----------------------------------------------------

void writeToUsb(const char* text, size_t len) {
  Serial.write(reinterpret_cast<const uint8_t*>(text), len);
}

// Increments a counter in EEPROM so every power-up gets a new boot ID.
uint32_t nextBootId() {
  uint32_t boot = 0;
  EEPROM.get(bootCounterAddr, boot);  // erased EEPROM reads 0xFFFFFFFF -> wraps to 0
  ++boot;
  EEPROM.put(bootCounterAddr, boot);
  return boot;
}

void loadDemoProfiles() {
  // Exercise IDs: 7 Goblet squats, 9 Lunges, 12 Push-ups.
  const char* const lines[] = {
    "PROFILES 3",
    "P 101 Alex",   "A 101 7 -1",
    "P 102 Jamie",  "A 102 9 -1",
    "P 103 Taylor", "A 103 12 -1",
    "PEND 3 3"
  };
  for (const char* line : lines) {
    pstore::handleCommand(line, strlen(line), true, 0);
  }
  pstore::takeUpdated();
}

// --- Drawing helpers ------------------------------------------------------------

void startOLED(const char* title) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(title);
}

// Map each section separately so the marker crosses a centre mark exactly
// when the knob crosses the corresponding scroll threshold.
int knobMarkerX() {
  if (knobPosition < leftThreshold) {
    return map(knobPosition, 0, leftThreshold - 1,
               knobTrackLeft, deadZoneLeftX - 1);
  }
  if (knobPosition > rightThreshold) {
    return map(knobPosition, rightThreshold + 1, knobMax,
               deadZoneRightX + 1, knobTrackRight);
  }
  return map(knobPosition, leftThreshold, rightThreshold,
             deadZoneLeftX, deadZoneRightX);
}

void refreshOLED(bool menuChanged) {
  int markerX = knobMarkerX();
  if (!menuChanged && markerX == displayedKnobX) {
    return;
  }

  // Erase only the bar so the marker can move without redrawing the menu.
  oled.fillRect(knobBarX, 0, knobBarWidth, 11, SSD1306_BLACK);
  oled.drawRect(knobBarX, 2, knobBarWidth, 7, SSD1306_WHITE);

  // The marks above and below the track bracket the no-scroll zone.
  int deadZoneWidth = deadZoneRightX - deadZoneLeftX + 1;
  oled.drawFastHLine(deadZoneLeftX, 0, deadZoneWidth, SSD1306_WHITE);
  oled.drawFastHLine(deadZoneLeftX, 10, deadZoneWidth, SSD1306_WHITE);
  oled.drawFastVLine(markerX, 3, 5, SSD1306_WHITE);
  displayedKnobX = markerX;
  oled.display();
}

void printScore(int score) {
  lcd.print(score);
  lcd.print("/");
  lcd.print(maxScore);
}

void lcdLines(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// Shows a message on the LCD; the current screen's LCD text returns after statusTime.
void showStatus(const char* line1, const char* line2) {
  lcdLines(line1, line2);
  statusActive = true;
  statusStart = millis();
}

uint16_t unsyncedFor(uint16_t participantId) {
  uint16_t n = 0;
  const uint16_t total = slog::entryCount();
  for (uint16_t i = 0; i < total; i++) {
    slog::Entry e;
    if (slog::getEntry(i, e) == slog::Status::Ok && !e.synced &&
        e.participantId == participantId) {
      n++;
    }
  }
  return n;
}

const char* catalogName(uint8_t exerciseId) {
  return exerciseId < catalogCount ? exerciseCatalog[exerciseId] : "?";
}

const pstore::Profile* currentProfile() {
  if (selectedUser < 0) {
    return nullptr;
  }
  return pstore::get(static_cast<uint8_t>(selectedUser));
}

// --- Participant list ---------------------------------------------------------

void showUsers() {
  if (!oledOk) {
    return;
  }
  startOLED("CCCare");
  const int userCount = pstore::count();

  if (userCount == 0) {
    oled.setCursor(0, 18);
    oled.print("No profiles yet.");
    oled.setCursor(0, 32);
    oled.print("Plug in by USB and");
    oled.setCursor(0, 42);
    oled.print("run Sync Device.");
    refreshOLED(true);
    lcdLines("No profiles", "Sync with laptop");
    return;
  }

  selectedUser = constrain(selectedUser, 0, userCount - 1);
  int firstRow = max(0, selectedUser - 1);
  for (int row = 0; row < 3 && firstRow + row < userCount; row++) {
    int index = firstRow + row;
    oled.setCursor(0, 18 + row * 14);
    oled.print(index == selectedUser ? "> " : "  ");
    oled.print(pstore::get(static_cast<uint8_t>(index))->label);
  }
  refreshOLED(true);

  const pstore::Profile* user = currentProfile();
  selectedUserId = user->id;
  char line2[40];  // the LCD shows the first 16 characters
  const uint16_t pending = unsyncedFor(user->id);
  if (pending > 0) {
    snprintf(line2, sizeof line2, "ID %u %u unsent", static_cast<unsigned>(user->id),
             static_cast<unsigned>(pending));
  } else {
    snprintf(line2, sizeof line2, "ID %u", static_cast<unsigned>(user->id));
  }
  lcdLines(user->label, line2);
}

// --- Profile and catalog menus ------------------------------------------------

bool alreadyHasExercise(int catalogIndex) {
  return pstore::findExercise(static_cast<uint8_t>(selectedUser),
                              static_cast<uint8_t>(catalogIndex)) >= 0;
}

// Profile rows are Back, all assigned exercises, and Add.
// Catalog rows are Back followed by the 55 available exercises.
int exerciseMenuSize() {
  if (screen == CatalogScreen) {
    return catalogCount + 1;
  }
  const pstore::Profile* user = currentProfile();
  return user == nullptr ? 1 : user->count + 2;
}

// The assignment highlighted on the profile screen, or nullptr.
const pstore::Assigned* selectedAssignment() {
  const pstore::Profile* user = currentProfile();
  if (user == nullptr || selectedExerciseRow < 1 || selectedExerciseRow > user->count) {
    return nullptr;
  }
  return &user->exercises[selectedExerciseRow - 1];
}

const char* exerciseRowLabel(int row) {
  if (row == 0) {
    return "< Back";
  }
  if (screen == CatalogScreen) {
    return (row - 1 < catalogCount) ? exerciseCatalog[row - 1] : "?";
  }
  if (row == exerciseMenuSize() - 1) {
    return "+ Add exercise";
  }
  const pstore::Profile* user = currentProfile();
  if (user == nullptr || row > user->count) {
    return "?";
  }
  return catalogName(user->exercises[row - 1].exerciseId);
}

// A 128px OLED fits 21 characters per line at text size 1.
// Leave two characters for the cursor and wrap long names at a space.
void drawExerciseRow(const char* label, int y, bool selected) {
  const int textWidth = 19;
  int split = strlen(label);

  if (split > textWidth) {
    split = textWidth;
    while (split > 0 && label[split] != ' ') {
      split--;
    }
    if (split == 0) {
      split = textWidth;
    }
  }

  oled.setCursor(0, y);
  oled.print(selected ? "> " : "  ");
  for (int i = 0; i < split; i++) {
    oled.print(label[i]);
  }
  if (label[split] != '\0') {
    int next = split;
    if (label[next] == ' ') {
      next++;
    }
    oled.setCursor(12, y + 8);
    oled.print(label + next);
  }
}

void showUsersFromAnywhere() {
  screen = UsersScreen;
  showUsers();
}

void showExerciseMenu() {
  if (!oledOk) {
    return;
  }
  const pstore::Profile* user = currentProfile();
  if (user == nullptr) {
    showUsersFromAnywhere();
    return;
  }
  bool inCatalog = screen == CatalogScreen;
  startOLED(inCatalog ? "Add exercise" : user->label);

  int menuSize = exerciseMenuSize();
  selectedExerciseRow = constrain(selectedExerciseRow, 0, menuSize - 1);
  int firstRow = constrain(selectedExerciseRow - 1, 0, max(0, menuSize - 3));
  for (int row = 0; row < 3 && firstRow + row < menuSize; row++) {
    int index = firstRow + row;
    drawExerciseRow(exerciseRowLabel(index), 12 + row * 17,
                    index == selectedExerciseRow);
  }
  refreshOLED(true);

  lcd.clear();
  lcd.setCursor(0, 0);
  const pstore::Assigned* assigned = inCatalog ? nullptr : selectedAssignment();
  lcd.print(assigned != nullptr ? "Press to edit" : user->label);
  lcd.setCursor(0, 1);

  if (selectedExerciseRow == 0) {
    lcd.print("Press to return");
  } else if (inCatalog) {
    bool exists = alreadyHasExercise(selectedExerciseRow - 1);
    lcd.print(exists ? "Already added" : "Press to add");
  } else if (assigned == nullptr) {
    lcd.print("Press to browse");
  } else if (assigned->score == noScore) {
    lcd.print("No score yet");
  } else {
    lcd.print("Score ");
    printScore(assigned->score);
  }
}

// --- Score editor -----------------------------------------------------------------

void showDraftScore() {
  lcd.setCursor(0, 0);
  lcd.print("Score ");
  printScore(draftScore);
  lcd.print("  ");  // Erase leftover digits when a shorter score is displayed.
}

void drawScoreEditor() {
  if (!oledOk) {
    return;
  }
  const pstore::Profile* user = currentProfile();
  if (user == nullptr || selectedAssignment() == nullptr) {
    showUsersFromAnywhere();
    return;
  }
  // Keep the user and full exercise name visible on the OLED while editing.
  startOLED("Edit score");
  oled.setCursor(0, 12);
  oled.print(user->label);
  drawExerciseRow(catalogName(selectedAssignment()->exerciseId), 27, true);
  oled.setCursor(0, 53);
  oled.print("Left -  Right +");
  refreshOLED(true);

  lcd.clear();
  showDraftScore();
  lcd.setCursor(0, 1);
  lcd.print("Press to save");
}

void startEditingScore() {
  const pstore::Assigned* assigned = selectedAssignment();
  if (assigned == nullptr) {
    return;
  }
  draftScore = assigned->score == noScore ? 0 : assigned->score;
  screen = ScoreScreen;
  drawScoreEditor();
}

// Logs one entry for the next sync, then updates the score shown on the device.
void saveDraftScore() {
  const pstore::Profile* user = currentProfile();
  const pstore::Assigned* assigned = selectedAssignment();
  if (user == nullptr || assigned == nullptr) {
    showUsersFromAnywhere();
    return;
  }
  const uint8_t exerciseId = assigned->exerciseId;
  const slog::Status saved = slog::addEntry(user->id, exerciseId, 0,
                                            static_cast<uint8_t>(draftScore));
  if (saved == slog::Status::Ok) {
    pstore::setScore(static_cast<uint8_t>(selectedUser), exerciseId,
                     static_cast<int8_t>(draftScore));
  }

  screen = ProfileScreen;
  showExerciseMenu();

  char line2[40];  // the LCD shows the first 16 characters
  switch (saved) {
    case slog::Status::Ok:
      snprintf(line2, sizeof line2, "%u to sync", static_cast<unsigned>(slog::unsyncedCount()));
      showStatus("Score saved", line2);
      break;
    case slog::Status::Full:
      lcd.setRGB(255, 0, 0);
      showStatus("Device full!", "Sync to laptop");
      break;
    default:
      showStatus("Not saved", "Try again");
      break;
  }
}

// --- Navigation -----------------------------------------------------------------

// Redraws whatever screen is showing (used after a status message expires).
void redrawScreen() {
  if (!oledOk) {
    lcdLines("OLED not found", "Check wiring");
    return;
  }
  switch (screen) {
    case UsersScreen:
      showUsers();
      break;
    case ProfileScreen:
    case CatalogScreen:
      showExerciseMenu();
      break;
    case ScoreScreen:
      drawScoreEditor();
      break;
  }
}

// Ends a status message early and restores the screen underneath it.
void endStatus() {
  if (!statusActive) {
    return;
  }
  statusActive = false;
  if (!slog::awaitingAck()) {
    lcd.setRGB(255, 255, 255);
  }
  redrawScreen();
}

// The physical Return button and on-screen Back row use the same navigation.
void goBack() {
  switch (screen) {
    case UsersScreen:
      break;

    case ProfileScreen:
      showUsersFromAnywhere();
      break;

    case CatalogScreen:
      screen = ProfileScreen;
      selectedExerciseRow = exerciseMenuSize() - 1;
      showExerciseMenu();
      break;

    case ScoreScreen:
      // Leave the saved score alone and return to the same exercise.
      screen = ProfileScreen;
      showExerciseMenu();
      break;
  }
}

void selectExerciseMenuRow() {
  if (selectedExerciseRow == 0) {
    goBack();
    return;
  }

  if (screen == ProfileScreen) {
    if (selectedExerciseRow == exerciseMenuSize() - 1) {
      screen = CatalogScreen;
      selectedExerciseRow = 1;
      showExerciseMenu();
    } else {
      startEditingScore();
    }
    return;
  }

  int catalogIndex = selectedExerciseRow - 1;
  if (alreadyHasExercise(catalogIndex)) {
    return;
  }
  // Append an unscored assignment and return with that row highlighted.
  // It is logged (and reaches the laptop) once a score is saved for it.
  const uint8_t user = static_cast<uint8_t>(selectedUser);
  if (pstore::assign(user, static_cast<uint8_t>(catalogIndex)) != pstore::Result::Ok) {
    return;
  }
  screen = ProfileScreen;
  selectedExerciseRow = pstore::findExercise(user, static_cast<uint8_t>(catalogIndex)) + 1;
  showExerciseMenu();
}

// --- Sync ---------------------------------------------------------------------------

// A new profile list replaced the old one: put back scores saved since the
// last upload, reselect the same participant, and return to the list.
void applyNewProfiles() {
  const uint16_t total = slog::entryCount();
  for (uint16_t i = 0; i < total; i++) {
    slog::Entry e;
    if (slog::getEntry(i, e) != slog::Status::Ok || e.synced) {
      continue;
    }
    const int index = pstore::findById(e.participantId);
    if (index >= 0) {
      pstore::setScore(static_cast<uint8_t>(index), e.exerciseId,
                       static_cast<int8_t>(e.technique));
    }
  }

  const int index = pstore::findById(selectedUserId);
  selectedUser = index >= 0 ? index : 0;
  selectedExerciseRow = 0;
  showUsersFromAnywhere();

  char line2[40];  // the LCD shows the first 16 characters
  if (slog::safeToPowerOff()) {
    snprintf(line2, sizeof line2, "Safe to unplug");
  } else {
    snprintf(line2, sizeof line2, "%u to sync", static_cast<unsigned>(slog::unsyncedCount()));
  }
  lcd.setRGB(0, 255, 0);
  showStatus("Profiles loaded", line2);
}

void serviceSync() {
  const unsigned long now = millis();
  for (uint8_t i = 0; i < maxSerialBytesPerLoop && Serial.available() > 0; ++i) {
    const int c = Serial.read();
    if (c >= 0) {
      slog::feed(static_cast<char>(c), now);
    }
  }
  slog::poll(now);
  pstore::poll(now);

  char line2[40];  // the LCD shows the first 16 characters
  switch (slog::takeEvent()) {
    case slog::Event::SyncStarted:
      lcd.setRGB(0, 0, 255);
      showStatus("Syncing...", "Keep plugged in");
      break;
    case slog::Event::SyncConfirmed:
      lcd.setRGB(0, 255, 0);
      snprintf(line2, sizeof line2, "%u saved", static_cast<unsigned>(slog::lastSyncCount()));
      showStatus("Sync complete", line2);
      break;
    case slog::Event::SyncTimedOut:
      lcd.setRGB(255, 128, 0);
      showStatus("Sync failed", "Try again");
      break;
    case slog::Event::ProtocolError:
    case slog::Event::None:
    default:
      break;
  }

  if (pstore::takeUpdated()) {
    applyNewProfiles();
  }

  if (statusActive && now - statusStart >= statusTime) {
    endStatus();
  }
}

// --- Arduino entry points --------------------------------------------------------

void setup() {
  // Start from the participant list (also makes setup() safe to call again).
  screen = UsersScreen;
  selectedUser = 0;
  selectedUserId = 0;
  selectedExerciseRow = 0;
  draftScore = 0;
  statusActive = false;
  scrollDirection = 0;
  displayedKnobX = -1;

  Serial.begin(115200);
  enterButton.begin();
  returnButton.begin();

  Wire.begin();
  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);

  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextWrap(false);

  slog::begin(nextBootId(), writeToUsb);
  pstore::begin(nullptr);  // no replies while loading the built-in profiles
#if DEMO_PROFILES
  loadDemoProfiles();
#endif
  pstore::setReply(slog::sendFramed);
  slog::setCommandHandler(pstore::handleCommand);

  if (!oledOk) {
    lcd.setRGB(255, 0, 0);
    lcdLines("OLED not found", "Check wiring");
    return;
  }
  knobPosition = knobMax - analogRead(A2);
  showUsers();
}

void loop() {
  if (!oledOk) {
    // Without the OLED the menus can't be used, but sync still works so no
    // saved scores are stranded on the device.
    serviceSync();
    delay(20);
    return;
  }
  serviceSync();

  // This potentiometer's raw reading decreases when turned right.
  knobPosition = knobMax - analogRead(A2);
  unsigned long now = millis();
  bool pressed = enterButton.pressed(now);
  bool back = returnButton.pressed(now);

  int direction = 0;
  unsigned long interval = slowScrollInterval;

  // A larger turn shortens the repeat interval for scrolling and scoring.
  if (knobPosition < leftThreshold) {
    direction = -1;
    interval = map(knobPosition, leftThreshold - 1, 0,
                   slowScrollInterval, fastScrollInterval);
  } else if (knobPosition > rightThreshold) {
    direction = 1;
    interval = map(knobPosition, rightThreshold + 1, knobMax,
                   slowScrollInterval, fastScrollInterval);
  }

  bool directionChanged = direction != scrollDirection;
  scrollDirection = direction;
  // Take one step as soon as the knob leaves centre, then repeat while held.
  bool shouldStep = direction != 0 &&
      (directionChanged || now - lastScrollTime >= interval);

  if (shouldStep) {
    lastScrollTime = now;
  }
  // Return works at any knob position and takes priority over Enter.
  if (back && screen != UsersScreen) {
    statusActive = false;  // goBack() redraws the whole screen
    if (!slog::awaitingAck()) {
      lcd.setRGB(255, 255, 255);
    }
    goBack();
    delay(20);
    return;
  }
  bool enter = pressed && direction == 0;
  if (shouldStep || enter) {
    endStatus();  // any input dismisses a status message
  }

  switch (screen) {
    case UsersScreen: {
      const int userCount = pstore::count();
      if (userCount == 0) {
        break;  // nothing to scroll or open until profiles arrive
      }
      if (shouldStep) {
        selectedUser = (selectedUser + direction + userCount) % userCount;
        showUsers();
      }
      if (enter) {
        screen = ProfileScreen;
        selectedExerciseRow = 1;
        showExerciseMenu();
      }
      break;
    }

    case ProfileScreen:
    case CatalogScreen:
      if (shouldStep) {
        int next = constrain(selectedExerciseRow + direction,
                             0, exerciseMenuSize() - 1);
        if (next != selectedExerciseRow) {
          selectedExerciseRow = next;
          showExerciseMenu();
        }
      }
      if (enter) {
        selectExerciseMenuRow();
      }
      break;

    case ScoreScreen:
      if (shouldStep) {
        int nextScore = constrain(draftScore + direction, 0, maxScore);
        if (nextScore != draftScore) {
          draftScore = nextScore;
          showDraftScore();
        }
      }
      if (enter) {
        saveDraftScore();
      }
      break;
  }

  // Move the marker even when the knob stays within the dead zone.
  refreshOLED(false);
  delay(20);
}
