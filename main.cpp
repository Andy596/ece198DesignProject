#include <Wire.h>
#include "rgb_lcd.h"
 
rgb_lcd lcd;
 
struct ExerciseRecord {
  const char* name;
  const char* exercise;
  int score;
};
 
const int recordCount = 3;
 
ExerciseRecord records[recordCount] = {
  {"Alex", "Squat", 86},
  {"Jamie", "Lunge", 92},
  {"Taylor", "Push-up", 78}
};
 
int displayedRecord = -1;
 
void setup() {
  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);
}
 
void loop() {
  int knob = analogRead(A2);
  int selectedRecord = knob * recordCount / 1024;
 
  if (selectedRecord != displayedRecord) {
    lcd.clear();
 
    lcd.setCursor(0, 0);
    lcd.print(records[selectedRecord].name);
 
    lcd.setCursor(0, 1);
    lcd.print(records[selectedRecord].exercise);
    lcd.print(" ");
    lcd.print(records[selectedRecord].score);
    lcd.print("/100");
 
    displayedRecord = selectedRecord;
  }
 
  delay(80);
}
