// CCCARE profile and exercise prototype.
// The OLED shows menus and knob position. The Grove LCD shows planned and completed values.
// Turn the knob to scroll, then centre it before pressing Enter (A0).
// Return (A1) backs out of menus and cancels unsaved edits.
// The Grove touch sensor (A3) saves edits or starts USB sync on the main menu.
#include <Wire.h>
#include "rgb_lcd.h"
#include <Adafruit_GFX.h>
// Avoid the legacy color macros that conflict with the Grove LCD library.
#define NO_ADAFRUIT_SSD1306_COLOR_COMPATIBILITY
#include <Adafruit_SSD1306.h>

rgb_lcd lcd;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// All profiles share the same catalog labels. Assignments store their indexes.
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

// Each dosage belongs to one assigned exercise. A zero set count means unset.
enum DosageMeasure : uint8_t { Reps, Seconds };
enum LoadUnit : uint8_t { Kilograms, Pounds };

struct Dosage {
  uint8_t sets;
  uint16_t amount;        // Reps or seconds per set.
  uint16_t loadQuarters;  // Load per set in quarter units, with no floating point.
  DosageMeasure measure;
  LoadUnit loadUnit;
};

const int maxSets = 99;
const int maxAmount = 999;
const int maxLoadQuarters = 3999;  // 999.75 kg or lb is the display/input limit.

struct AssignedExercise {
  uint8_t catalogIndex;
  Dosage dosage;
};

struct UserProfile {
  String name;
  String firstExercise;  // Starter exercise for a sample profile, when present.
  Dosage firstDosage;
  AssignedExercise addedExercises[catalogCount];
  int addedCount;
  unsigned long id;
  unsigned long session;
};

// A completed entry keeps a copy of the prescription at the first save.
// Names stay with the entry even if the profile or assignment is removed.
struct CompletedEntry {
  unsigned long participantId;
  unsigned long session;
  char participant[13];
  char exercise[32];
  Dosage prescribed;
  Dosage actual;
};

const int maxCompletedEntries = 64;
CompletedEntry completedEntries[maxCompletedEntries];
int completedCount = 0;
unsigned long nextParticipantId = 4;
unsigned long nextSession = 1;

// Profiles and dosages are kept in RAM until the board restarts.
const int maxProfiles = 10;
int profileCount = 3;
// Fictional profiles with sample dosages for demonstrations.
UserProfile profiles[maxProfiles] = {
  {"Alex", "Squat", {2, 8, 0, Reps, Kilograms}, {}, 0, 1, 0},
  {"Jamie", "Lunges", {1, 10, 0, Reps, Kilograms}, {}, 0, 2, 0},
  {"Taylor", "Push-ups", {2, 6, 0, Reps, Kilograms}, {}, 0, 3, 0}
};

enum Screen {
  UserList, UserEditor, Profile, Catalog, ExerciseActions, DosageEditor,
  SessionHistory, SessionExercises, CompletedHistory, DeleteUser, DeleteExercise, UsbSync
};
enum ExerciseAction { ExerciseBack, EditPlan, LogCompleted, ViewHistory };
enum DosageField { SetsField, MeasureField, AmountField, UnitField, LoadField, ReviewField };
Screen screen = UserList;

int selectedUser = 0;
int selectedDeleteUser = 0;
int selectedExerciseRow = 0;
int selectedExerciseAction = EditPlan;
int selectedHistoryEntry = -1;
Screen historyReturnScreen = ExerciseActions;

// These indexes are rebuilt when a participant opens their session history.
unsigned long historySessions[maxCompletedEntries];
int historySessionCount = 0;
int selectedHistorySession = 0;
uint8_t historySessionEntries[maxCompletedEntries];
int historySessionEntryCount = 0;
int selectedSessionEntry = 0;

bool loggingCompletion = false;
// Edits stay in this draft until Enter is pressed on the review screen.
Dosage draftDosage = {};
DosageField activeDosageField = SetsField;

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

// The Grove buttons and digital touch sensor are active HIGH and debounce independently.
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
GroveButton touchSensor(A3);

// Compare names one character at a time, ignoring uppercase/lowercase.
// If one name is a prefix of another, the shorter name comes first.
// Return a negative value when first should appear before second.
int compareNames(const String& first, const String& second) {
  unsigned int i = 0;
  while (i < first.length() && i < second.length()) {
    char a = first[i];
    char b = second[i];
    if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
    if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
    if (a < b) return -1;
    if (a > b) return 1;
    i++;
  }
  if (first.length() < second.length()) return -1;
  if (first.length() > second.length()) return 1;
  return 0;
}

// Insertion sort moves whole profiles so exercises and dosages stay attached.
// Equal names keep their previous order. Menu actions are not sorted.
void sortUsersAlphabetically() {
  for (int i = 1; i < profileCount; i++) {
    int j = i;
    while (j > 0 && compareNames(profiles[j].name, profiles[j - 1].name) < 0) {
      UserProfile temporary = profiles[j - 1];
      profiles[j - 1] = profiles[j];
      profiles[j] = temporary;

      // Keep the same user highlighted when their index changes.
      if (selectedUser == j) {
        selectedUser = j - 1;
      } else if (selectedUser == j - 1) {
        selectedUser = j;
      }
      j--;
    }
  }
}

// New profiles start with a name and an empty exercise list.
String draftName;
const unsigned int nameLimit = 12;
// Letters, space, hyphen, delete, save, and cancel.
const char letterKeys[] = "abcdefghijklmnopqrstuvwxyz -<>^";
int selectedKey = 0;
const char* editorMessage = nullptr;

// The cursor blinks independently of knob and button input.
const unsigned long nameCursorInterval = 500;
unsigned long nameCursorChangedAt = 0;
bool nameCursorVisible = true;

const unsigned long usbSendTimeout = 8000;
const unsigned long usbAckTimeout = 10000;
const unsigned long usbResultDuration = 2500;
unsigned long usbChangedAt = 0;
unsigned long usbTransferId = 0;
bool waitingForUsbAck = false;
char usbReply[32];
size_t usbReplyLength = 0;
bool discardUsbReply = false;

void resetNameCursor() {
  nameCursorVisible = true;
  nameCursorChangedAt = millis();
}

void drawNameCursor() {
  int x = draftName.length() * 6;
  int color = nameCursorVisible ? SSD1306_WHITE : SSD1306_BLACK;
  oled.drawFastVLine(x, 16, 7, color);
}

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

const char* dosageAmountUnit(const Dosage& dosage) {
  if (dosage.measure == Seconds) {
    return "sec";
  }
  return dosage.amount == 1 ? "rep" : "reps";
}

const char* dosageLoadUnit(const Dosage& dosage) {
  return dosage.loadUnit == Kilograms ? "kg" : "lb";
}

const char* const loadFractions[] = {"", ".25", ".5", ".75"};

void formatDosageVolume(char* line, size_t size, const Dosage& dosage) {
  snprintf(line, size, "%u x %u %s", static_cast<unsigned>(dosage.sets),
           static_cast<unsigned>(dosage.amount), dosageAmountUnit(dosage));
}

void formatDosageLoad(char* line, size_t size, const Dosage& dosage) {
  snprintf(line, size, "%u%s %s", static_cast<unsigned>(dosage.loadQuarters / 4),
           loadFractions[dosage.loadQuarters % 4], dosageLoadUnit(dosage));
}

void showDosageSummary(const Dosage& dosage) {
  char line[17];
  lcd.setCursor(0, 0);
  formatDosageVolume(line, sizeof(line), dosage);
  lcd.print(line);
  lcd.setCursor(0, 1);
  formatDosageLoad(line, sizeof(line), dosage);
  lcd.print("Load ");
  lcd.print(line);
}

void showUsers() {
  startOLED("CCCare");

  int menuSize = profileCount + 2;  // Profile rows, Delete user, Add user.
  int firstRow = constrain(selectedUser - 1, 0, max(0, menuSize - 4));
  for (int row = 0; row < 4 && firstRow + row < menuSize; row++) {
    int index = firstRow + row;
    oled.setCursor(0, 14 + row * 14);
    oled.print(index == selectedUser ? "> " : "  ");

    if (index == profileCount) {
      oled.print("- Delete user");
    } else if (index == profileCount + 1) {
      oled.print("+ Add user");
    } else {
      oled.print(profiles[index].name);
    }
  }
  refreshOLED(true);

  lcd.clear();
  lcd.setCursor(0, 0);

  if (selectedUser == profileCount) {
    lcd.print("Delete user");
    lcd.setCursor(0, 1);
    lcd.print(profileCount == 0 ? "No users" : "Press to choose");
  } else if (selectedUser == profileCount + 1) {
    lcd.print("Add user");
    lcd.setCursor(0, 1);
    if (profileCount >= maxProfiles) {
      lcd.print("User list full");
    } else {
      lcd.print("Press to add");
    }
  } else {
    const UserProfile& user = profiles[selectedUser];
    lcd.print(user.name);
    lcd.setCursor(0, 1);

    int exerciseCount = user.addedCount + (user.firstExercise.length() > 0);
    lcd.print(exerciseCount);
    lcd.print(exerciseCount == 1 ? " exercise" : " exercises");
  }
}

// A separate list lets the user choose which profile to remove.
// Row zero is Back; the remaining rows are existing users.
void showDeleteUsers() {
  startOLED("Delete user");
  int menuSize = profileCount + 1;
  int firstRow = constrain(selectedDeleteUser - 1, 0, max(0, menuSize - 3));
  for (int row = 0; row < 3 && firstRow + row < menuSize; row++) {
    int index = firstRow + row;
    oled.setCursor(0, 18 + row * 14);
    oled.print(index == selectedDeleteUser ? "> " : "  ");
    oled.print(index == 0 ? "< Back" : profiles[index - 1].name.c_str());
  }
  refreshOLED(true);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(selectedDeleteUser == 0 ? "Delete user" :
            profiles[selectedDeleteUser - 1].name.c_str());
  lcd.setCursor(0, 1);
  lcd.print(selectedDeleteUser == 0 ? "Press to return" : "Press to delete");
}

bool alreadyHasExercise(int catalogIndex) {
  const UserProfile& user = profiles[selectedUser];
  if (user.firstExercise.equalsIgnoreCase(exerciseCatalog[catalogIndex])) {
    return true;
  }

  for (int i = 0; i < user.addedCount; i++) {
    if (user.addedExercises[i].catalogIndex == catalogIndex) {
      return true;
    }
  }

  return false;
}

// Row zero is Back. If present, the sample exercise precedes catalog assignments.
int firstCatalogRow() {
  return profiles[selectedUser].firstExercise.length() > 0 ? 2 : 1;
}

int assignedExerciseCount() {
  return firstCatalogRow() - 1 + profiles[selectedUser].addedCount;
}

int exerciseMenuSize() {
  if (screen == Catalog) {
    return catalogCount + 1;
  }
  // Profile actions follow the exercise rows. Deletion shows only exercises and Back.
  return assignedExerciseCount() + (screen == DeleteExercise ? 1 : 5);
}

const char* exerciseRowLabel(int row) {
  if (row == 0) {
    return "< Back";
  }
  if (screen == Catalog) {
    return exerciseCatalog[row - 1];
  }

  const UserProfile& user = profiles[selectedUser];
  int firstRow = firstCatalogRow();
  if (row < firstRow) {
    return user.firstExercise.c_str();
  }
  if (screen != DeleteExercise) {
    int exerciseCount = assignedExerciseCount();
    if (row == exerciseCount + 1) {
      return "New session";
    }
    if (row == exerciseCount + 2) {
      return "Session history";
    }
    if (row == exerciseCount + 3) {
      return "- Delete exercise";
    }
    if (row == exerciseCount + 4) {
      return "+ Add exercise";
    }
  }
  return exerciseCatalog[user.addedExercises[row - firstRow].catalogIndex];
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

// The selected row stays fixed while editing so Save updates that same exercise.
Dosage& selectedExerciseDosage() {
  UserProfile& user = profiles[selectedUser];
  int firstRow = firstCatalogRow();
  if (selectedExerciseRow < firstRow) {
    return user.firstDosage;
  }
  return user.addedExercises[selectedExerciseRow - firstRow].dosage;
}

void showExerciseMenu() {
  const UserProfile& user = profiles[selectedUser];
  bool inCatalog = screen == Catalog;
  bool deleting = screen == DeleteExercise;
  startOLED(inCatalog ? "Add exercise" : deleting ? "Delete ex." : user.name.c_str());

  int menuSize = exerciseMenuSize();
  int firstRow = constrain(selectedExerciseRow - 1, 0, max(0, menuSize - 3));
  for (int row = 0; row < 3 && firstRow + row < menuSize; row++) {
    int index = firstRow + row;
    drawExerciseRow(exerciseRowLabel(index), 12 + row * 17,
                    index == selectedExerciseRow);
  }
  refreshOLED(true);

  lcd.clear();
  int exerciseCount = assignedExerciseCount();
  if (!inCatalog && !deleting && selectedExerciseRow > 0 &&
      selectedExerciseRow <= exerciseCount) {
    const Dosage& dosage = selectedExerciseDosage();
    if (dosage.sets > 0) {
      showDosageSummary(dosage);
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Press to set");
      lcd.setCursor(0, 1);
      lcd.print("No dosage set");
    }
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print(user.name);
  lcd.setCursor(0, 1);
  if (selectedExerciseRow == 0) {
    lcd.print("Press to return");
  } else if (inCatalog) {
    bool exists = alreadyHasExercise(selectedExerciseRow - 1);
    lcd.print(exists ? "Already added" : "Press to add");
  } else if (deleting) {
    lcd.print("Press to delete");
  } else if (selectedExerciseRow == exerciseCount + 1) {
    if (user.session == 0) {
      lcd.print("Start a session");
    } else {
      lcd.print("Session ");
      lcd.print(user.session);
    }
  } else if (selectedExerciseRow == exerciseCount + 2) {
    lcd.print("Press to browse");
  } else if (selectedExerciseRow == exerciseCount + 3) {
    lcd.print(exerciseCount == 0 ? "No exercises" : "Press to choose");
  } else {
    lcd.print("Press to browse");
  }
}

bool entryMatchesExercise(const CompletedEntry& entry) {
  return entry.participantId == profiles[selectedUser].id &&
         strcmp(entry.exercise, exerciseRowLabel(selectedExerciseRow)) == 0;
}

int currentCompletedEntry() {
  unsigned long session = profiles[selectedUser].session;
  for (int i = completedCount - 1; i >= 0; i--) {
    if (completedEntries[i].session == session &&
        entryMatchesExercise(completedEntries[i])) {
      return i;
    }
  }
  return -1;
}

int findHistoryEntry(int from, int direction) {
  for (int i = from + direction; i >= 0 && i < completedCount; i += direction) {
    if (entryMatchesExercise(completedEntries[i])) {
      return i;
    }
  }
  return -1;
}

void showExerciseActions() {
  const char* const actions[] = {"< Back", "Edit dosage", "Log completed", "View history"};
  const UserProfile& user = profiles[selectedUser];
  startOLED(user.name.c_str());
  drawExerciseRow(exerciseRowLabel(selectedExerciseRow), 11, false);
  for (int i = ExerciseBack; i <= ViewHistory; i++) {
    oled.setCursor(0, 29 + i * 9);
    oled.print(i == selectedExerciseAction ? "> " : "  ");
    oled.print(actions[i]);
  }
  refreshOLED(true);

  lcd.clear();
  lcd.setCursor(0, 0);
  if (selectedExerciseAction == ExerciseBack) {
    lcd.print("Press to return");
  } else if (selectedExerciseAction == EditPlan) {
    const Dosage& plan = selectedExerciseDosage();
    if (plan.sets > 0) {
      showDosageSummary(plan);
    } else {
      lcd.print("No dosage set");
    }
  } else if (selectedExerciseAction == LogCompleted) {
    if (user.session == 0) {
      lcd.print("First session");
    } else {
      lcd.print("Session ");
      lcd.print(user.session);
    }
    lcd.setCursor(0, 1);
    lcd.print(currentCompletedEntry() >= 0 ? "Press to update" : "Press to log");
  } else {
    lcd.print(findHistoryEntry(completedCount, -1) >= 0 ?
              "Press to review" : "No entries yet");
  }
}

void showSessionHistory() {
  const UserProfile& user = profiles[selectedUser];
  startOLED(user.name.c_str());

  lcd.clear();
  lcd.setCursor(0, 0);
  if (historySessionCount == 0) {
    oled.setCursor(0, 22);
    oled.print("No saved sessions");
    refreshOLED(true);
    lcd.print("No sessions yet");
    lcd.setCursor(0, 1);
    lcd.print("A1: Back");
    return;
  }

  int firstRow = constrain(selectedHistorySession - 1, 0, max(0, historySessionCount - 3));
  for (int row = 0; row < 3 && firstRow + row < historySessionCount; row++) {
    int index = firstRow + row;
    oled.setCursor(0, 15 + row * 13);
    oled.print(index == selectedHistorySession ? "> " : "  ");
    oled.print("Session ");
    oled.print(historySessions[index]);
  }
  oled.setCursor(0, 56);
  oled.print("Left older Right new");
  refreshOLED(true);

  unsigned long session = historySessions[selectedHistorySession];
  // The shortened label leaves room for any session number on the 16-column LCD.
  lcd.print(session > 99999999UL ? "S " : "Session ");
  lcd.print(session);
  lcd.setCursor(0, 1);
  lcd.print("Press to open");
}

void startSessionHistory() {
  const UserProfile& user = profiles[selectedUser];
  historySessionCount = 0;

  // A participant's new entries are added in session order. Corrections
  // overwrite entries in place, so each session needs only one list row.
  for (int i = 0; i < completedCount; i++) {
    const CompletedEntry& entry = completedEntries[i];
    if (entry.participantId == user.id &&
        (historySessionCount == 0 || historySessions[historySessionCount - 1] != entry.session)) {
      historySessions[historySessionCount++] = entry.session;
    }
  }

  selectedHistorySession = max(0, historySessionCount - 1);
  screen = SessionHistory;
  showSessionHistory();
}

void showSessionExercises() {
  startOLED("Session items");
  int firstRow = constrain(selectedSessionEntry - 1, 0, max(0, historySessionEntryCount - 3));
  for (int row = 0; row < 3 && firstRow + row < historySessionEntryCount; row++) {
    int index = firstRow + row;
    const CompletedEntry& entry = completedEntries[historySessionEntries[index]];
    drawExerciseRow(entry.exercise, 12 + row * 17, index == selectedSessionEntry);
  }
  refreshOLED(true);

  lcd.clear();
  showDosageSummary(completedEntries[historySessionEntries[selectedSessionEntry]].actual);
}

void startSessionExercises() {
  unsigned long participantId = profiles[selectedUser].id;
  unsigned long session = historySessions[selectedHistorySession];
  historySessionEntryCount = 0;
  for (int i = 0; i < completedCount; i++) {
    const CompletedEntry& entry = completedEntries[i];
    if (entry.participantId == participantId && entry.session == session) {
      historySessionEntries[historySessionEntryCount++] = i;
    }
  }

  if (historySessionEntryCount > 0) {
    selectedSessionEntry = 0;
    screen = SessionExercises;
    showSessionExercises();
  }
}

void showCompletedHistory() {
  const CompletedEntry& entry = completedEntries[selectedHistoryEntry];
  char title[15];
  snprintf(title, sizeof(title), entry.session > 999999UL ? "S %lu" : "Session %lu",
           entry.session);
  startOLED(title);
  drawExerciseRow(entry.exercise, 11, false);

  // Put the completed values first so changes are visible while browsing.
  char line[17];
  oled.setCursor(0, 28);
  oled.print("Done ");
  formatDosageVolume(line, sizeof(line), entry.actual);
  oled.print(line);
  oled.setCursor(0, 36);
  oled.print("Done load ");
  formatDosageLoad(line, sizeof(line), entry.actual);
  oled.print(line);

  oled.setCursor(0, 45);
  oled.print("Plan ");
  if (entry.prescribed.sets > 0) {
    formatDosageVolume(line, sizeof(line), entry.prescribed);
    oled.print(line);
    oled.setCursor(0, 53);
    oled.print("Plan load ");
    formatDosageLoad(line, sizeof(line), entry.prescribed);
    oled.print(line);
  } else {
    oled.print("unset");
  }
  refreshOLED(true);
  lcd.clear();
  showDosageSummary(entry.actual);
}

void showDosageField() {
  lcd.clear();
  lcd.setCursor(0, 0);
  switch (activeDosageField) {
    case SetsField:
      lcd.print("Sets");
      lcd.setCursor(0, 1);
      lcd.print(static_cast<int>(draftDosage.sets));
      break;
    case MeasureField:
      lcd.print("Reps or seconds");
      lcd.setCursor(0, 1);
      lcd.print(draftDosage.measure == Reps ? "Repetitions" : "Seconds");
      break;
    case AmountField:
      lcd.print(draftDosage.measure == Reps ? "Reps per set" : "Seconds per set");
      lcd.setCursor(0, 1);
      lcd.print(draftDosage.amount);
      break;
    case UnitField:
      lcd.print("Load unit");
      lcd.setCursor(0, 1);
      lcd.print(draftDosage.loadUnit == Kilograms ? "Kilograms" : "Pounds");
      break;
    case LoadField: {
      char line[17];
      lcd.print("Load per set");
      lcd.setCursor(0, 1);
      formatDosageLoad(line, sizeof(line), draftDosage);
      lcd.print(line);
      break;
    }
    case ReviewField:
      showDosageSummary(draftDosage);
      break;
  }
}

void showDosageEditor() {
  bool review = activeDosageField == ReviewField;
  startOLED(loggingCompletion ? (review ? "Review logged" : "Log completed") :
            (review ? "Review dosage" : "Edit dosage"));
  oled.setCursor(0, 12);
  oled.print(profiles[selectedUser].name);
  drawExerciseRow(exerciseRowLabel(selectedExerciseRow), 27, true);
  oled.setCursor(0, 53);
  oled.print(review ? "A0/A3 save A1 cancel" :
             loggingCompletion ? "A0 next A3 review" : "A0 next A3 save");
  refreshOLED(true);
  showDosageField();
}

void startEditingDosage(bool completed) {
  loggingCompletion = completed;
  if (completed) {
    int index = currentCompletedEntry();
    const Dosage& plan = selectedExerciseDosage();
    // Copy the units only so a new log cannot mistake the plan for work done.
    draftDosage = index >= 0 ? completedEntries[index].actual :
                  Dosage{0, 0, 0, plan.measure, plan.loadUnit};
  } else {
    draftDosage = selectedExerciseDosage();
  }
  activeDosageField = SetsField;
  screen = DosageEditor;
  showDosageEditor();
}

void adjustDosage(int direction) {
  // Turning past the outer quarter of travel gives a larger step for long
  // durations and heavier loads. A small turn still gives precise control.
  bool fast = knobPosition < knobMax / 4 || knobPosition > 3 * knobMax / 4;
  switch (activeDosageField) {
    case SetsField:
      draftDosage.sets = constrain(static_cast<int>(draftDosage.sets) + direction, 0, maxSets);
      break;
    case MeasureField:
      draftDosage.measure = direction < 0 ? Reps : Seconds;
      break;
    case AmountField:
      draftDosage.amount = constrain(static_cast<int>(draftDosage.amount) +
                                     direction * (fast ? 10 : 1), 0, maxAmount);
      break;
    case UnitField:
      draftDosage.loadUnit = direction < 0 ? Kilograms : Pounds;
      break;
    case LoadField:
      draftDosage.loadQuarters = constrain(static_cast<int>(draftDosage.loadQuarters) +
                                           direction * (fast ? 20 : 1), 0, maxLoadQuarters);
      break;
    case ReviewField:
      return;
  }
  showDosageField();
}

bool saveCompletedEntry() {
  int index = currentCompletedEntry();
  if (index >= 0) {
    completedEntries[index].actual = draftDosage;
    return true;
  }
  if (completedCount >= maxCompletedEntries) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Session log full");
    lcd.setCursor(0, 1);
    lcd.print("No entry saved");
    return false;
  }

  UserProfile& user = profiles[selectedUser];
  if (user.session == 0) {
    user.session = nextSession++;
  }
  CompletedEntry& entry = completedEntries[completedCount++];
  entry.participantId = user.id;
  entry.session = user.session;
  snprintf(entry.participant, sizeof(entry.participant), "%s", user.name.c_str());
  snprintf(entry.exercise, sizeof(entry.exercise), "%s", exerciseRowLabel(selectedExerciseRow));
  entry.prescribed = selectedExerciseDosage();
  entry.actual = draftDosage;
  return true;
}

void saveDosage() {
  if (draftDosage.sets == 0 || draftDosage.amount == 0) {
    activeDosageField = draftDosage.sets == 0 ? SetsField : AmountField;
    showDosageEditor();
    lcd.setCursor(0, 1);
    lcd.print("Enter 1 or more");
    return;
  }

  if (loggingCompletion) {
    // A3 can jump to review. An explicit save on review commits the entry.
    if (activeDosageField != ReviewField) {
      activeDosageField = ReviewField;
      showDosageEditor();
      return;
    }
    if (!saveCompletedEntry()) {
      return;
    }
  } else {
    selectedExerciseDosage() = draftDosage;
  }
  screen = ExerciseActions;
  showExerciseActions();
}

void advanceDosageEditor() {
  if ((activeDosageField == SetsField && draftDosage.sets == 0) ||
      (activeDosageField == AmountField && draftDosage.amount == 0)) {
    lcd.setCursor(0, 1);
    lcd.print("Enter 1 or more");
    return;
  }

  if (activeDosageField == ReviewField) {
    saveDosage();
  } else {
    activeDosageField = static_cast<DosageField>(activeDosageField + 1);
    showDosageEditor();
  }
}

// The physical Return button and on-screen Back key use the same navigation.
void goBack() {
  switch (screen) {
    case UserList:
      break;

    case UsbSync:
      waitingForUsbAck = false;
      screen = UserList;
      showUsers();
      break;

    case UserEditor:
      draftName = "";
      screen = UserList;
      showUsers();
      break;

    case DeleteUser:
      screen = UserList;
      selectedUser = profileCount;
      showUsers();
      break;

    case Profile:
      screen = UserList;
      showUsers();
      break;

    case Catalog:
      screen = Profile;
      selectedExerciseRow = exerciseMenuSize() - 1;
      showExerciseMenu();
      break;

    case DeleteExercise:
      screen = Profile;
      selectedExerciseRow = exerciseMenuSize() - 2;
      showExerciseMenu();
      break;

    case ExerciseActions:
      screen = Profile;
      showExerciseMenu();
      break;

    case DosageEditor:
      screen = ExerciseActions;
      showExerciseActions();
      break;

    case SessionHistory:
      screen = Profile;
      showExerciseMenu();
      break;

    case SessionExercises:
      screen = SessionHistory;
      showSessionHistory();
      break;

    case CompletedHistory:
      screen = historyReturnScreen;
      if (screen == SessionExercises) {
        showSessionExercises();
      } else {
        showExerciseActions();
      }
      break;
  }
}

void deleteSelectedExercise() {
  UserProfile& user = profiles[selectedUser];
  int firstRow = firstCatalogRow();

  if (selectedExerciseRow < firstRow) {
    user.firstExercise = "";
    user.firstDosage = {};
  } else {
    int index = selectedExerciseRow - firstRow;
    // Move each remaining exercise and its dosage together.
    for (int i = index; i + 1 < user.addedCount; i++) {
      user.addedExercises[i] = user.addedExercises[i + 1];
    }
    user.addedCount--;
    user.addedExercises[user.addedCount] = {};
  }

  goBack();
}

void selectExerciseMenuRow() {
  if (selectedExerciseRow == 0) {
    goBack();
    return;
  }

  if (screen == DeleteExercise) {
    deleteSelectedExercise();
    return;
  }

  if (screen == Profile) {
    int exerciseCount = assignedExerciseCount();
    if (selectedExerciseRow == exerciseCount + 1) {
      profiles[selectedUser].session = nextSession++;
      showExerciseMenu();
    } else if (selectedExerciseRow == exerciseCount + 2) {
      startSessionHistory();
    } else if (selectedExerciseRow == exerciseCount + 3) {
      if (exerciseCount > 0) {
        screen = DeleteExercise;
        selectedExerciseRow = 1;
        showExerciseMenu();
      }
    } else if (selectedExerciseRow == exerciseCount + 4) {
      screen = Catalog;
      selectedExerciseRow = 1;
      showExerciseMenu();
    } else {
      selectedExerciseAction = EditPlan;
      screen = ExerciseActions;
      showExerciseActions();
    }
    return;
  }

  UserProfile& user = profiles[selectedUser];
  int catalogIndex = selectedExerciseRow - 1;
  if (alreadyHasExercise(catalogIndex) || user.addedCount >= catalogCount) {
    return;
  }

  // Add the exercise with an unset dosage and highlight its profile row.
  AssignedExercise& exercise = user.addedExercises[user.addedCount];
  exercise.catalogIndex = catalogIndex;
  exercise.dosage = {};
  user.addedCount++;
  screen = Profile;
  selectedExerciseRow = firstCatalogRow() + user.addedCount - 1;
  showExerciseMenu();
}

void showKeyboard() {
  int keyCount = strlen(letterKeys);
  char key = letterKeys[selectedKey];
  startOLED("Enter name");
  oled.setCursor(0, 16);
  oled.print(draftName);
  drawNameCursor();

  for (int position = 0; position < 5; position++) {
    int index = (selectedKey + position - 2 + keyCount) % keyCount;
    int x = position * 25;
    if (position == 2) {
      oled.fillRect(x, 32, 23, 20, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }
    oled.setTextSize(2);
    oled.setCursor(x + 5, 34);
    oled.print(letterKeys[index]);
  }
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 56);
  if (editorMessage != nullptr) {
    oled.print(editorMessage);
  } else if (key == '<') {
    oled.print("Button: delete");
  } else if (key == '>') {
    oled.print("Button: save user");
  } else if (key == '^') {
    oled.print("Button: cancel");
  } else if (key == ' ') {
    oled.print("Button: enter space");
  } else {
    oled.print("Button: enter ");
    oled.print(key);
  }
  refreshOLED(true);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tap A3 to save");
  lcd.setCursor(0, 1);
  lcd.print(draftName);
}

void startAddingUser() {
  screen = UserEditor;
  selectedKey = 0;
  editorMessage = nullptr;
  draftName = "";
  resetNameCursor();
  showKeyboard();
}

// Save a name from either the > key or the touch sensor.
// Touch saves regardless of knob position, but still validates the name.
void saveNewUser() {
  editorMessage = nullptr;
  draftName.trim();
  if (draftName.length() == 0) {
    editorMessage = "Name is empty";
  } else if (profileCount >= maxProfiles) {
    editorMessage = "User list full";
  } else {
    // Reset the entire slot: deleted profiles must not leave old data.
    profiles[profileCount] = UserProfile{};
    UserProfile& user = profiles[profileCount];
    user.name = draftName;
    user.id = nextParticipantId++;
    selectedUser = profileCount;
    profileCount++;
    sortUsersAlphabetically();  // Sort immediately after saving a name.
    draftName = "";
    screen = UserList;
    showUsers();
    return;
  }
  showKeyboard();  // Show feedback if the name is empty or the list is full.
}

void enterKey() {
  char key = letterKeys[selectedKey];
  editorMessage = nullptr;
  if (key == '^') {
    goBack();
    return;
  }
  if (key == '<') {
    if (draftName.length() > 0) {
      draftName.remove(draftName.length() - 1);
      resetNameCursor();
    }
  } else if (key == '>') {
    saveNewUser();
    return;
  } else if (draftName.length() < nameLimit) {
    // Uppercase the first letter after the start or a space.
    if (key >= 'a' && key <= 'z' &&
        (draftName.length() == 0 || draftName[draftName.length() - 1] == ' ')) {
      key = key - 'a' + 'A';
    }
    draftName += key;
    resetNameCursor();
  } else {
    editorMessage = "Limit reached: use >";
  }
  showKeyboard();
}

void deleteSelectedUser() {
  int index = selectedDeleteUser - 1;
  for (int i = index; i < profileCount - 1; i++) {
    profiles[i] = profiles[i + 1];
  }
  profileCount--;
  profiles[profileCount] = UserProfile{};
  screen = UserList;
  selectedUser = profileCount;  // Highlight Delete user after deletion.
  showUsers();
}

void showUsbStatus(const char* firstLine, const char* secondLine) {
  startOLED("USB sync");
  oled.setCursor(0, 18);
  oled.print(firstLine);
  oled.setCursor(0, 33);
  oled.print(secondLine);
  refreshOLED(true);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(firstLine);
  lcd.setCursor(0, 1);
  lcd.print(secondLine);
}

void finishUsbSync(const char* firstLine, const char* secondLine) {
  waitingForUsbAck = false;
  usbChangedAt = millis();
  showUsbStatus(firstLine, secondLine);
}

// Only write bytes that fit in the USB transmit buffer. A stalled receiver
// must not leave the device stuck partway through writing the snapshot.
bool writeUsbLine(const char* line, unsigned long startedAt) {
  size_t remaining = strlen(line);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(line);
  while (remaining > 0) {
    if (!Serial || millis() - startedAt >= usbSendTimeout) {
      return false;
    }
    int space = Serial.availableForWrite();
    if (space <= 0) {
      delay(1);
      continue;
    }
    size_t writable = min(remaining, static_cast<size_t>(space));
    size_t sent = Serial.write(data, writable);
    remaining -= sent;
    data += sent;
    if (sent == 0) {
      delay(1);
    }
  }
  return true;
}

// Names use the on-device keyboard and exercise labels come from the catalog
// or starter profiles. They contain no double quotes or line breaks.
void formatCsvDosage(char* line, size_t size, const Dosage& dosage) {
  if (dosage.sets == 0) {
    snprintf(line, size, ",,,,");
  } else {
    snprintf(line, size, "%u,%u,%s,%u%s,%s", static_cast<unsigned>(dosage.sets),
             static_cast<unsigned>(dosage.amount), dosage.measure == Reps ? "reps" : "sec",
             static_cast<unsigned>(dosage.loadQuarters / 4),
             loadFractions[dosage.loadQuarters % 4], dosageLoadUnit(dosage));
  }
}

bool writeUsbRecord(const char* kind, unsigned long participantId, const char* name,
                    const char* exercise, unsigned long session,
                    const Dosage& plan, const Dosage& actual, unsigned long startedAt) {
  char plannedFields[32];
  char actualFields[32];
  char sessionField[12] = "";
  char line[160];
  formatCsvDosage(plannedFields, sizeof(plannedFields), plan);
  formatCsvDosage(actualFields, sizeof(actualFields), actual);
  if (session > 0) {
    snprintf(sessionField, sizeof(sessionField), "%lu", session);
  }
  snprintf(line, sizeof(line), "%s,%lu,\"%s\",\"%s\",%s,%s,%s\n",
           kind, participantId, name, exercise, sessionField, plannedFields, actualFields);
  return writeUsbLine(line, startedAt);
}

bool writeDosageSnapshot(unsigned long startedAt) {
  int rowCount = completedCount;
  for (int i = 0; i < profileCount; i++) {
    const UserProfile& user = profiles[i];
    int exercises = user.addedCount + (user.firstExercise.length() > 0);
    rowCount += max(1, exercises);
  }

  char line[48];
  snprintf(line, sizeof(line), "DOSAGE_BEGIN %lu %d\n", usbTransferId, rowCount);
  if (!writeUsbLine(line, startedAt) ||
      !writeUsbLine("record,participant_id,participant,exercise,session,"
                    "planned_sets,planned_amount,planned_measure,planned_load,planned_unit,"
                    "actual_sets,actual_amount,actual_measure,actual_load,actual_unit\n",
                    startedAt)) {
    return false;
  }

  const Dosage unset = {};
  for (int i = 0; i < profileCount; i++) {
    const UserProfile& user = profiles[i];
    if (user.firstExercise.length() > 0) {
      if (!writeUsbRecord("plan", user.id, user.name.c_str(), user.firstExercise.c_str(),
                          0, user.firstDosage, unset, startedAt)) {
        return false;
      }
    } else if (user.addedCount == 0 &&
               !writeUsbRecord("plan", user.id, user.name.c_str(), "", 0, unset, unset, startedAt)) {
      return false;
    }

    for (int j = 0; j < user.addedCount; j++) {
      const AssignedExercise& exercise = user.addedExercises[j];
      if (!writeUsbRecord("plan", user.id, user.name.c_str(), exerciseCatalog[exercise.catalogIndex],
                          0, exercise.dosage, unset, startedAt)) {
        return false;
      }
    }
  }

  for (int i = 0; i < completedCount; i++) {
    const CompletedEntry& entry = completedEntries[i];
    if (!writeUsbRecord("completed", entry.participantId, entry.participant, entry.exercise,
                        entry.session, entry.prescribed, entry.actual, startedAt)) {
      return false;
    }
  }

  snprintf(line, sizeof(line), "DOSAGE_END %lu\n", usbTransferId);
  return writeUsbLine(line, startedAt);
}

void startUsbSync() {
  screen = UsbSync;
  if (!Serial) {
    finishUsbSync("No USB receiver", "Open sync tool");
    return;
  }

  // Discard stale replies while allowing the new transfer its own ID.
  for (int pending = Serial.available(); pending > 0; pending--) {
    Serial.read();
  }
  usbTransferId++;
  usbReplyLength = 0;
  discardUsbReply = false;
  showUsbStatus("Sending records", "Keep USB plugged");
  if (!writeDosageSnapshot(millis())) {
    finishUsbSync("USB send failed", "Retry from menu");
    return;
  }

  waitingForUsbAck = true;
  usbChangedAt = millis();
}

void pollUsbSync(unsigned long now) {
  if (!waitingForUsbAck) {
    if (now - usbChangedAt >= usbResultDuration) {
      goBack();
    }
    return;
  }

  if (now - usbChangedAt >= usbAckTimeout) {
    finishUsbSync("No save reply", "Retry from menu");
    return;
  }

  for (int pending = Serial.available(); pending > 0; pending--) {
    int next = Serial.read();
    if (next == '\n') {
      usbReply[usbReplyLength] = '\0';
      if (!discardUsbReply) {
        char expected[32];
        snprintf(expected, sizeof(expected), "ACK %lu", usbTransferId);
        if (strcmp(usbReply, expected) == 0) {
          finishUsbSync("USB sync done", "Saved on laptop");
          return;
        }
        snprintf(expected, sizeof(expected), "ERR %lu", usbTransferId);
        if (strcmp(usbReply, expected) == 0) {
          finishUsbSync("Laptop save fail", "Retry from menu");
          return;
        }
      }
      usbReplyLength = 0;
      discardUsbReply = false;
    } else if (next >= 0 && next != '\r') {
      if (usbReplyLength + 1 < sizeof(usbReply)) {
        usbReply[usbReplyLength++] = static_cast<char>(next);
      } else {
        discardUsbReply = true;
      }
    }
  }
}

void setup() {
  sortUsersAlphabetically();
  enterButton.begin();
  returnButton.begin();
  touchSensor.begin();
  Serial.begin(115200);

  Wire.begin();
  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextWrap(false);
  knobPosition = knobMax - analogRead(A2);
  showUsers();
}

void loop() {
  // This potentiometer's raw reading decreases when turned right.
  knobPosition = knobMax - analogRead(A2);
  unsigned long now = millis();
  bool pressed = enterButton.pressed(now);
  bool back = returnButton.pressed(now);
  bool touched = touchSensor.pressed(now);

  int direction = 0;
  unsigned long interval = slowScrollInterval;

  // A larger turn shortens the repeat interval for scrolling and dosage adjustment.
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
  if (back && screen != UserList) {
    goBack();
    delay(20);
    return;
  }
  bool enter = pressed && direction == 0;

  switch (screen) {
    case UserList:
      if (touched) {
        startUsbSync();
        break;
      }
      if (shouldStep) {
        int next = constrain(selectedUser + direction, 0, profileCount + 1);
        if (next != selectedUser) {
          selectedUser = next;
          showUsers();
        }
      }
      if (enter) {
        if (selectedUser < profileCount) {
          screen = Profile;
          selectedExerciseRow = 1;
          showExerciseMenu();
        } else if (selectedUser == profileCount) {
          if (profileCount > 0) {
            screen = DeleteUser;
            selectedDeleteUser = 1;
            showDeleteUsers();
          }
        } else if (profileCount < maxProfiles) {
          startAddingUser();
        }
      }
      break;

    case DeleteUser:
      if (shouldStep) {
        int next = constrain(selectedDeleteUser + direction, 0, profileCount);
        if (next != selectedDeleteUser) {
          selectedDeleteUser = next;
          showDeleteUsers();
        }
      }
      if (enter) {
        if (selectedDeleteUser == 0) {
          goBack();
        } else {
          deleteSelectedUser();
        }
      }
      break;

    case UserEditor:
      // Touch saves the name independently of knob position.
      if (touched) {
        saveNewUser();
        break;
      }
      if (shouldStep) {
        int keyCount = strlen(letterKeys);
        selectedKey = (selectedKey + direction + keyCount) % keyCount;
        editorMessage = nullptr;
        showKeyboard();
      }
      if (enter) {
        enterKey();
      }
      break;

    case Profile:
    case Catalog:
    case DeleteExercise:
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

    case ExerciseActions:
      if (shouldStep) {
        int next = constrain(selectedExerciseAction + direction,
                             static_cast<int>(ExerciseBack), static_cast<int>(ViewHistory));
        if (next != selectedExerciseAction) {
          selectedExerciseAction = next;
          showExerciseActions();
        }
      }
      if (enter) {
        if (selectedExerciseAction == ExerciseBack) {
          goBack();
        } else if (selectedExerciseAction == EditPlan) {
          startEditingDosage(false);
        } else if (selectedExerciseAction == LogCompleted) {
          startEditingDosage(true);
        } else {
          int latest = findHistoryEntry(completedCount, -1);
          if (latest >= 0) {
            selectedHistoryEntry = latest;
            historyReturnScreen = ExerciseActions;
            screen = CompletedHistory;
            showCompletedHistory();
          }
        }
      }
      break;

    case SessionHistory:
      if (shouldStep && historySessionCount > 0) {
        int next = constrain(selectedHistorySession + direction, 0, historySessionCount - 1);
        if (next != selectedHistorySession) {
          selectedHistorySession = next;
          showSessionHistory();
        }
      }
      if (enter && historySessionCount > 0) {
        startSessionExercises();
      }
      break;

    case SessionExercises:
      if (shouldStep) {
        int next = constrain(selectedSessionEntry + direction, 0, historySessionEntryCount - 1);
        if (next != selectedSessionEntry) {
          selectedSessionEntry = next;
          showSessionExercises();
        }
      }
      if (enter) {
        selectedHistoryEntry = historySessionEntries[selectedSessionEntry];
        historyReturnScreen = SessionExercises;
        screen = CompletedHistory;
        showCompletedHistory();
      }
      break;

    case CompletedHistory:
      if (shouldStep && historyReturnScreen == SessionExercises) {
        int next = constrain(selectedSessionEntry + direction, 0, historySessionEntryCount - 1);
        if (next != selectedSessionEntry) {
          selectedSessionEntry = next;
          selectedHistoryEntry = historySessionEntries[next];
          showCompletedHistory();
        }
      } else if (shouldStep) {
        int next = findHistoryEntry(selectedHistoryEntry, direction);
        if (next >= 0) {
          selectedHistoryEntry = next;
          showCompletedHistory();
        }
      }
      if (enter) {
        goBack();
      }
      break;

    case DosageEditor:
      if (touched) {
        saveDosage();
        break;
      }
      if (shouldStep) {
        adjustDosage(direction);
      }
      if (enter) {
        advanceDosageEditor();
      }
      break;

    case UsbSync:
      pollUsbSync(now);
      break;
  }

  unsigned long cursorNow = millis();
  if (screen == UserEditor && cursorNow - nameCursorChangedAt >= nameCursorInterval) {
    nameCursorChangedAt = cursorNow;
    nameCursorVisible = !nameCursorVisible;
    drawNameCursor();
    refreshOLED(true);
  }

  // Move the marker even when the knob stays within the dead zone.
  refreshOLED(false);
  delay(20);
}
