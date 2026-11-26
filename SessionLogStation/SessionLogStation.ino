#include <Wire.h>
#include "rgb_lcd.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

rgb_lcd lcd;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

struct ExerciseRecord {
  String name;
  String exercise;
  int score;
};

const int maxRecords = 10;
int recordCount = 3;

ExerciseRecord records[maxRecords] = {
  {"Alex", "Squat", 86},
  {"Jamie", "Lunge", 92},
  {"Taylor", "Push-up", 78}
};

const int leftThreshold = 460;
const int rightThreshold = 563;
const int knobMax = 1023;

const unsigned long slowScrollInterval = 600;
const unsigned long fastScrollInterval = 120;

int selectedRecord = 0;
int scrollDirection = 0;
unsigned long lastScrollTime = 0;

// Grove button connected to A0.
// Pressed = HIGH, released = LOW.
const int buttonPin = A0;
const int buttonPressedLevel = HIGH;

// Grove Return button connected to A1, pressed = HIGH.
const int returnButtonPin = A1;
int lastReturnReading = LOW;
int stableReturnReading = LOW;
unsigned long lastReturnChange = 0;

int lastButtonReading = LOW;
int stableButtonReading = LOW;

unsigned long lastButtonChange = 0;
const unsigned long debounceTime = 35;

// New-user keyboard.
bool addingUser = false;

String fields[3] = {"", "", ""};
const int fieldLimits[3] = {6, 5, 3};
int activeField = 0;

// Lowercase letters, numbers, space, and editing controls.
const char letterKeys[] =
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789 -<>^";

const char numberKeys[] = "0123456789<>^";

int selectedKey = 0;
String editorMessage = "";

bool buttonPressed() {
  int reading = digitalRead(buttonPin);
  unsigned long now = millis();

  if (reading != lastButtonReading) {
    lastButtonChange = now;
    lastButtonReading = reading;
  }

  if (now - lastButtonChange >= debounceTime &&
      reading != stableButtonReading) {
    stableButtonReading = reading;

    return stableButtonReading == buttonPressedLevel;
  }

  return false;
}

bool returnButtonPressed() {
  int reading = digitalRead(returnButtonPin);
  unsigned long now = millis();

  if (reading != lastReturnReading) {
    lastReturnChange = now;
    lastReturnReading = reading;
  }

  if (now - lastReturnChange >= debounceTime &&
      reading != stableReturnReading) {
    stableReturnReading = reading;
    return stableReturnReading == HIGH;
  }

  return false;
}

const char* currentKeys() {
  if (activeField == 2) {
    return numberKeys;
  }

  return letterKeys;
}

String entryText() {
  // The program supplies both commas.
  return fields[0] + "," + fields[1] + "," + fields[2];
}

void showNames() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);

  if (scrollDirection != 0) {
    oled.print("CCCare - Scrolling");
  } else {
    oled.print("CCCare - Chosen");
  }

  int menuCount = recordCount + 1;
  int firstRecord = selectedRecord - 1;

  if (firstRecord < 0) {
    firstRecord = 0;
  }

  for (int row = 0; row < 3; row++) {
    int index = firstRecord + row;

    if (index < menuCount) {
      oled.setCursor(0, 18 + row * 14);

      if (index == selectedRecord) {
        oled.print("> ");
      } else {
        oled.print("  ");
      }

      if (index == recordCount) {
        oled.print("+ Add user");
      } else {
        oled.print(records[index].name);
      }
    }
  }

  oled.display();
}

void showScore() {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (selectedRecord == recordCount) {
    lcd.print("Add user");
    lcd.setCursor(0, 1);

    if (recordCount >= maxRecords) {
      lcd.print("User list full");
    } else if (scrollDirection != 0) {
      lcd.print("Selecting...");
    } else {
      lcd.print("Press to add");
    }

    return;
  }

  lcd.print(records[selectedRecord].name);
  lcd.setCursor(0, 1);

  if (scrollDirection != 0) {
    lcd.print("Selecting...");
  } else {
    lcd.print(records[selectedRecord].exercise);
    lcd.print(" ");
    lcd.print(records[selectedRecord].score);
    lcd.print("/100");
  }
}

void showKeyboard() {
  const char* keys = currentKeys();
  int keyCount = strlen(keys);
  char key = keys[selectedKey];

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);

  if (activeField == 0) {
    oled.print("Enter name");
  } else if (activeField == 1) {
    oled.print("Enter exercise");
  } else {
    oled.print("Enter score");
  }

  oled.setCursor(0, 12);
  oled.print(entryText());

  // Show five keys with the selected key in the middle.
  for (int position = 0; position < 5; position++) {
    int index =
        (selectedKey + position - 2 + keyCount) % keyCount;

    int x = position * 25;

    if (position == 2) {
      oled.fillRect(x, 26, 23, 20, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }

    oled.setTextSize(2);
    oled.setCursor(x + 5, 28);
    oled.print(keys[index]);
  }

  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 53);

  if (editorMessage.length() > 0) {
    oled.print(editorMessage);
  } else if (key == '<') {
    oled.print("Button: delete");
  } else if (key == '>') {
    if (activeField == 2) {
      oled.print("Button: save user");
    } else {
      oled.print("Button: next field");
    }
  } else if (key == '^') {
    oled.print("Button: cancel");
  } else if (key == ' ') {
    oled.print("Button: enter space");
  } else {
    oled.print("Button: enter ");
    oled.print(key);
  }

  oled.display();

  lcd.clear();
  lcd.setCursor(0, 0);

  if (activeField == 0) {
    lcd.print("Enter name");
  } else if (activeField == 1) {
    lcd.print("Enter exercise");
  } else {
    lcd.print("Enter score");
  }

  lcd.setCursor(0, 1);
  lcd.print(entryText());
}

void startAddingUser() {
  addingUser = true;
  activeField = 0;
  selectedKey = 0;
  editorMessage = "";

  for (int i = 0; i < 3; i++) {
    fields[i] = "";
  }

  showKeyboard();
}

void finishEditing() {
  addingUser = false;
  showNames();
  showScore();
}

void enterKey() {
  char key = currentKeys()[selectedKey];
  editorMessage = "";

  if (key == '^') {
    finishEditing();
    return;
  }

  if (key == '<') {
    if (fields[activeField].length() > 0) {
      fields[activeField].remove(
          fields[activeField].length() - 1);
    } else if (activeField > 0) {
      activeField--;
      selectedKey = 0;
    }
  } else if (key == '>') {
    fields[activeField].trim();

    if (fields[activeField].length() == 0) {
      editorMessage = "Field is empty";
    } else if (activeField < 2) {
      activeField++;
      selectedKey = 0;
    } else {
      int score = fields[2].toInt();

      if (score > 100) {
        editorMessage = "Score must be 0-100";
      } else if (recordCount >= maxRecords) {
        editorMessage = "User list full";
      } else {
        records[recordCount].name = fields[0];
        records[recordCount].exercise = fields[1];
        records[recordCount].score = score;

        selectedRecord = recordCount;
        recordCount++;

        finishEditing();
        return;
      }
    }
  } else {
    if (fields[activeField].length() <
        fieldLimits[activeField]) {

      // Capitalize the first letter of each name word.
      if (activeField == 0) {
        int length = fields[0].length();
        bool startOfWord = length == 0;

        if (length > 0) {
          startOfWord = fields[0][length - 1] == ' ';
        }

        if (startOfWord && key >= 'a' && key <= 'z') {
          key = key - 'a' + 'A';
        }
      }

      fields[activeField] += key;
    } else {
      editorMessage = "Limit reached: use >";
    }
  }

  showKeyboard();
}

void setup() {
  pinMode(buttonPin, INPUT);
  pinMode(returnButtonPin, INPUT);
  lastReturnReading = digitalRead(returnButtonPin);
  stableReturnReading = lastReturnReading;

  lastButtonReading = digitalRead(buttonPin);
  stableButtonReading = lastButtonReading;

  Wire.begin();

  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextWrap(false);

  showNames();
  showScore();
}

void loop() {
  // This potentiometer's raw reading decreases to the right.
  int knob = knobMax - analogRead(A2);
  unsigned long now = millis();
  bool pressed = buttonPressed();
  bool returnPressed = returnButtonPressed();

  int direction = 0;
  unsigned long interval = slowScrollInterval;

  // Left moves up, centre holds, and right moves down.
  if (knob < leftThreshold) {
    direction = -1;

    interval = map(
        knob,
        leftThreshold - 1,
        0,
        slowScrollInterval,
        fastScrollInterval
    );
  } else if (knob > rightThreshold) {
    direction = 1;

    interval = map(
        knob,
        rightThreshold + 1,
        knobMax,
        slowScrollInterval,
        fastScrollInterval
    );
  }

  bool directionChanged = direction != scrollDirection;
  scrollDirection = direction;

  // Move immediately on a direction change, then repeat.
  bool shouldStep = direction != 0 &&
      (directionChanged || now - lastScrollTime >= interval);

  if (shouldStep) {
    lastScrollTime = now;
  }

  if (addingUser) {
    // Return cancels from any field, even while the knob is moving.
    // Handle Return before Enter so an unfinished record cannot be saved.
    if (returnPressed) {
      for (int i = 0; i < 3; i++) {
        fields[i] = "";
      }
      editorMessage = "";
      finishEditing();
      return;
    }

    if (shouldStep) {
      int keyCount = strlen(currentKeys());

      selectedKey =
          (selectedKey + direction + keyCount) % keyCount;

      editorMessage = "";
      showKeyboard();
    }

    if (pressed && direction == 0) {
      enterKey();
    }
  } else {
    if (shouldStep) {
      int menuCount = recordCount + 1;

      selectedRecord =
          (selectedRecord + direction + menuCount) % menuCount;
    }

    if (directionChanged || shouldStep) {
      showNames();
      showScore();
    }

    if (pressed &&
        direction == 0 &&
        selectedRecord == recordCount &&
        recordCount < maxRecords) {
      startAddingUser();
    }
  }

  delay(20);
}
