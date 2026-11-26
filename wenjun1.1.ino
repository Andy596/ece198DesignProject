// CCCARE profile and exercise prototype.
// The OLED shows menus and knob position. The Grove LCD shows scores and prompts.
// Turn the knob to scroll, then centre it before pressing Enter (A0).
// Return (A1) backs out of menus and cancels unsaved edits.
// Touch the Grove touch sensor (A3) to save the name from the name editor.
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

const int maxScore = 100;
const int noScore = -1;  // An assigned exercise can exist before it is scored.

struct AssignedExercise {
  uint8_t catalogIndex;
  int8_t score;  // Signed so it can hold both -1 and 0 through 100.
};

struct UserProfile {
  String name;
  String firstExercise;  // Blank when the first score is a general technique score.
  int8_t firstScore;
  AssignedExercise addedExercises[catalogCount];
  int addedCount;
};

// Profiles and scores are kept in RAM until the board restarts.
const int maxProfiles = 10;
int profileCount = 3;
UserProfile profiles[maxProfiles] = {
  {"Alex", "Squat", 86, {}, 0},
  {"Jamie", "Lunges", 92, {}, 0},
  {"Taylor", "Push-ups", 78, {}, 0}
};

enum Screen { UserList, UserEditor, Profile, Catalog, ScoreEditor, DeleteUser, DeleteExercise };
Screen screen = UserList;

int selectedUser = 0;
int selectedDeleteUser = 0;
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

// Grove buttons and the digital touch sensor are active HIGH.
// Each input is debounced independently.
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
GroveButton touchSensor(A3);  // Digital Grove touch sensor: touch = HIGH.

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

// Insertion sort moves whole profiles so exercises and scores stay attached.
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

// Add user asks for a name only. Scores are entered from the profile later.
String draftName;
const unsigned int nameLimit = 12;
// Name keyboard: letters, space, hyphen, delete, save, and cancel.
const char letterKeys[] =
    "abcdefghijklmnopqrstuvwxyz"
    " -<>^";
int selectedKey = 0;
const char* editorMessage = nullptr;

const char* currentKeys() {
  return letterKeys;
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

void printScore(int score) {
  lcd.print(score);
  lcd.print("/");
  lcd.print(maxScore);
}

// Users are followed by Delete user and Add user, in that order.
void showUsers() {
  startOLED("CCCare");
  int menuSize = profileCount + 2;
  int firstRow = constrain(selectedUser - 1, 0, max(0, menuSize - 3));
  for (int row = 0; row < 3 && firstRow + row < menuSize; row++) {
    int index = firstRow + row;
    oled.setCursor(0, 18 + row * 14);
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
    lcd.print(profileCount >= maxProfiles ? "User list full" : "Press to add");
  } else {
    const UserProfile& user = profiles[selectedUser];
    lcd.print(user.name);
    lcd.setCursor(0, 1);
    if (user.firstScore != noScore) {
      lcd.print(user.firstExercise);
      lcd.print(" ");
      printScore(user.firstScore);
    } else {
      lcd.print("Press to open");
    }
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

// Default profiles may have an original exercise. New profiles start empty.
bool hasFirstExercise() {
  const UserProfile& user = profiles[selectedUser];
  return user.firstExercise.length() > 0 || user.firstScore != noScore;
}

int assignedExerciseCount() {
  return profiles[selectedUser].addedCount + (hasFirstExercise() ? 1 : 0);
}

// Profile: Back, exercises, Delete exercise, Add exercise.
// Delete exercise: Back followed by assigned exercises only.
int exerciseMenuSize() {
  if (screen == Catalog) {
    return catalogCount + 1;
  }
  if (screen == DeleteExercise) {
    return assignedExerciseCount() + 1;
  }
  return assignedExerciseCount() + 3;
}

const char* exerciseRowLabel(int row) {
  if (row == 0) {
    return "< Back";
  }
  if (screen == Catalog) {
    return exerciseCatalog[row - 1];
  }
  int count = assignedExerciseCount();
  if (screen != DeleteExercise) {
    if (row == count + 1) return "- Delete exercise";
    if (row == count + 2) return "+ Add exercise";
  }
  const UserProfile& user = profiles[selectedUser];
  if (hasFirstExercise() && row == 1) {
    return user.firstExercise.length() == 0 ?
        "Technique score" : user.firstExercise.c_str();
  }
  int index = row - 1 - (hasFirstExercise() ? 1 : 0);
  return exerciseCatalog[user.addedExercises[index].catalogIndex];
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

// The highlighted exercise row stays selected throughout editing and saving.
// Return a reference so reading and saving use the same profile and exercise.
int8_t& selectedExerciseScore() {
  UserProfile& user = profiles[selectedUser];
  if (hasFirstExercise() && selectedExerciseRow == 1) {
    return user.firstScore;
  }
  int index = selectedExerciseRow - 1 - (hasFirstExercise() ? 1 : 0);
  return user.addedExercises[index].score;
}

void showExerciseMenu() {
  const UserProfile& user = profiles[selectedUser];
  bool inCatalog = screen == Catalog;
  bool deleting = screen == DeleteExercise;
  startOLED(inCatalog ? "Add exercise" :
            deleting ? "Delete ex." : user.name.c_str());

  int menuSize = exerciseMenuSize();
  int firstRow = constrain(selectedExerciseRow - 1, 0, max(0, menuSize - 3));
  for (int row = 0; row < 3 && firstRow + row < menuSize; row++) {
    int index = firstRow + row;
    drawExerciseRow(exerciseRowLabel(index), 12 + row * 17,
                    index == selectedExerciseRow);
  }
  refreshOLED(true);

  lcd.clear();
  lcd.setCursor(0, 0);
  bool onExercise = !inCatalog && selectedExerciseRow > 0 &&
      selectedExerciseRow <= assignedExerciseCount();
  lcd.print(onExercise ? (deleting ? "Delete exercise" : "Press to edit") :
            user.name.c_str());
  lcd.setCursor(0, 1);

  if (selectedExerciseRow == 0) {
    lcd.print("Press to return");
  } else if (inCatalog) {
    lcd.print(alreadyHasExercise(selectedExerciseRow - 1) ?
              "Already added" : "Press to add");
  } else if (deleting) {
    lcd.print("Press to delete");
  } else if (selectedExerciseRow == assignedExerciseCount() + 1) {
    lcd.print(assignedExerciseCount() == 0 ? "No exercises" : "Press to choose");
  } else if (selectedExerciseRow == menuSize - 1) {
    lcd.print("Press to browse");
  } else {
    int score = selectedExerciseScore();
    if (score == noScore) {
      lcd.print("No score yet");
    } else {
      lcd.print("Score ");
      printScore(score);
    }
  }
}

void showDraftScore() {
  lcd.setCursor(0, 0);
  lcd.print("Score ");
  printScore(draftScore);
  lcd.print("  ");  // Erase leftover digits when a shorter score is displayed.
}

void startEditingScore() {
  int savedScore = selectedExerciseScore();
  draftScore = savedScore == noScore ? 0 : savedScore;
  screen = ScoreEditor;

  // Keep the user and full exercise name visible on the OLED while editing.
  startOLED("Edit score");
  oled.setCursor(0, 12);
  oled.print(profiles[selectedUser].name);
  drawExerciseRow(exerciseRowLabel(selectedExerciseRow), 27, true);
  oled.setCursor(0, 53);
  oled.print("Left -  Right +");
  refreshOLED(true);

  lcd.clear();
  showDraftScore();
  lcd.setCursor(0, 1);
  lcd.print("Press to save");
}

// The physical Return button and on-screen Back key use the same navigation.
void goBack() {
  switch (screen) {
    case UserList:
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
    case ScoreEditor:
      // Return discards the draft score.
      screen = Profile;
      showExerciseMenu();
      break;
  }
}

void selectExerciseMenuRow() {
  if (selectedExerciseRow == 0) {
    goBack();
    return;
  }

  if (screen == DeleteExercise) {
    UserProfile& user = profiles[selectedUser];
    if (hasFirstExercise() && selectedExerciseRow == 1) {
      user.firstExercise = "";
      user.firstScore = noScore;
    } else {
      int index = selectedExerciseRow - 1 - (hasFirstExercise() ? 1 : 0);
      for (int i = index; i < user.addedCount - 1; i++) {
        user.addedExercises[i] = user.addedExercises[i + 1];
      }
      user.addedCount--;
    }
    screen = Profile;
    selectedExerciseRow = exerciseMenuSize() - 2;
    showExerciseMenu();
    return;
  }

  if (screen == Profile) {
    int count = assignedExerciseCount();
    if (selectedExerciseRow == count + 1) {
      if (count > 0) {
        screen = DeleteExercise;
        selectedExerciseRow = 1;
        showExerciseMenu();
      }
    } else if (selectedExerciseRow == count + 2) {
      screen = Catalog;
      selectedExerciseRow = 1;
      showExerciseMenu();
    } else {
      startEditingScore();
    }
    return;
  }

  UserProfile& user = profiles[selectedUser];
  int catalogIndex = selectedExerciseRow - 1;
  if (alreadyHasExercise(catalogIndex) || user.addedCount >= catalogCount) {
    return;
  }
  // New assignments have no score until edited from the profile.
  AssignedExercise& exercise = user.addedExercises[user.addedCount];
  exercise.catalogIndex = catalogIndex;
  exercise.score = noScore;
  user.addedCount++;
  screen = Profile;
  selectedExerciseRow = assignedExerciseCount();
  showExerciseMenu();
}

// The new-user editor contains only a name field.
void showKeyboard() {
  const char* keys = currentKeys();
  int keyCount = strlen(keys);
  char key = keys[selectedKey];
  startOLED("Enter name");
  oled.setCursor(0, 16);
  oled.print(draftName);

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
    oled.print(keys[index]);
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
  lcd.print("Enter name");
  lcd.setCursor(0, 1);
  lcd.print(draftName);
}

void startAddingUser() {
  screen = UserEditor;
  selectedKey = 0;
  editorMessage = nullptr;
  draftName = "";
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
    user.firstScore = noScore;
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
  char key = currentKeys()[selectedKey];
  editorMessage = nullptr;
  if (key == '^') {
    goBack();
    return;
  }
  if (key == '<') {
    if (draftName.length() > 0) {
      draftName.remove(draftName.length() - 1);
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
  profiles[profileCount].firstScore = noScore;
  screen = UserList;
  selectedUser = profileCount;  // Highlight Delete user after deletion.
  showUsers();
}

void setup() {
  sortUsersAlphabetically();  // Keep the initial profiles sorted too.
  enterButton.begin();
  returnButton.begin();
  touchSensor.begin();

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
  if (back && screen != UserList) {
    goBack();
    delay(20);
    return;
  }
  bool enter = pressed && direction == 0;

  switch (screen) {
    case UserList:
      if (shouldStep) {
        // Stop at the first user and at Add user; never wrap the menu.
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
        if (selectedDeleteUser == 0) goBack();
        else deleteSelectedUser();
      }
      break;

    case UserEditor:
      // Save before processing keyboard scrolling or Enter.
      // A held touch produces only one event; release before touching again.
      if (touched) {
        saveNewUser();
        break;
      }
      if (shouldStep) {
        int keyCount = strlen(currentKeys());
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

    case ScoreEditor:
      if (shouldStep) {
        int nextScore = constrain(draftScore + direction, 0, maxScore);
        if (nextScore != draftScore) {
          draftScore = nextScore;
          showDraftScore();
        }
      }
      if (enter) {
        selectedExerciseScore() = draftScore;
        screen = Profile;
        showExerciseMenu();
      }
      break;
  }

  // Move the marker even when the knob stays within the dead zone.
  refreshOLED(false);
  delay(20);
}
