// Host stand-in for Adafruit_SSD1306. Text is kept on a character grid
// (6x8 px cells at text size 1) so tests can read what the OLED shows.
#pragma once

#include <stdint.h>

#include "Wire.h"

constexpr uint16_t SSD1306_BLACK = 0;
constexpr uint16_t SSD1306_WHITE = 1;
constexpr uint8_t SSD1306_SWITCHCAPVCC = 2;

class Adafruit_SSD1306 {
 public:
  static constexpr int kCols = 21;  // 128 px / 6 px
  static constexpr int kRows = 8;   // 64 px / 8 px

  Adafruit_SSD1306(int w, int h, TwoWire* wire, int resetPin);
  bool begin(uint8_t vcc, uint8_t addr);
  void clearDisplay();
  void setTextSize(int size);
  void setTextColor(uint16_t color);
  void setTextColor(uint16_t fg, uint16_t bg);
  void setTextWrap(bool wrap);
  void setCursor(int x, int y);
  void print(const char* text);
  void print(char c);
  void print(int value);
  void display();
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void drawRect(int x, int y, int w, int h, uint16_t color);
  void drawFastHLine(int x, int y, int w, uint16_t color);
  void drawFastVLine(int x, int y, int h, uint16_t color);

  // --- Simulator only -------------------------------------------------------
  const char* shownRow(int row) const;  // text on the panel after the last display()
  int shownMarkerX() const { return shownMarkerX_; }
  unsigned long displayCount() const { return displayCount_; }
  // Characters drawn past the right edge, or text overlapping the knob bar.
  unsigned long overflowCount() const { return overflow_; }
  bool beginResult = true;  // set false to simulate a failed begin()

 private:
  void putChar(char c);
  void checkBounds(int x, int y, int w, int h);
  char draw_[kRows][kCols + 1];
  char shown_[kRows][kCols + 1];
  int x_ = 0;
  int y_ = 0;
  int textSize_ = 1;
  int markerX_ = -1;
  int shownMarkerX_ = -1;
  unsigned long displayCount_ = 0;
  unsigned long overflow_ = 0;
  bool begun_ = false;
};
