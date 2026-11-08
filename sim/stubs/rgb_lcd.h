// Host stand-in for the Grove rgb_lcd library: keeps a 16x2 text buffer and
// logs the screen to stderr whenever it changes.
#pragma once

#include <stdint.h>

class rgb_lcd {
 public:
  void begin(uint8_t cols, uint8_t rows);
  void setRGB(uint8_t r, uint8_t g, uint8_t b);
  void clear();
  void setCursor(uint8_t col, uint8_t row);
  void print(const char* text);
  void print(int value);
  // Simulator only: log the screen if it changed since the last call.
  void flushToLog();

 private:
  char screen_[2][17];
  char logged_[2][17];
  uint8_t col_ = 0;
  uint8_t row_ = 0;
  uint8_t r_ = 0, g_ = 0, b_ = 0;
  uint8_t loggedR_ = 1, loggedG_ = 1, loggedB_ = 1;
};
