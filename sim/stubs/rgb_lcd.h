// Host stand-in for the Grove rgb_lcd library: keeps a 16x2 text buffer.
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
  void print(unsigned int value);
  void print(long value);
  void print(unsigned long value);
  template <class S, class = decltype(static_cast<const S*>(nullptr)->c_str())>
  void print(const S& text) {
    print(text.c_str());
  }

  // --- Simulator only -------------------------------------------------------
  // Log the screen to stderr if it changed since the last call.
  void flushToLog();
  const char* line(int row) const { return screen_[row == 0 ? 0 : 1]; }
  uint8_t red() const { return r_; }
  uint8_t green() const { return g_; }
  uint8_t blue() const { return b_; }
  unsigned long overflowCount() const { return overflow_; }

 private:
  char screen_[2][17];
  char logged_[2][17];
  uint8_t col_ = 0;
  uint8_t row_ = 0;
  uint8_t r_ = 0, g_ = 0, b_ = 0;
  uint8_t loggedR_ = 1, loggedG_ = 1, loggedB_ = 1;
  unsigned long overflow_ = 0;
};
