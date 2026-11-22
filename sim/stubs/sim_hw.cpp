// sim_hw.cpp - host implementations of the stub Arduino hardware (time, pins,
// EEPROM, Grove LCD, SSD1306 OLED). Serial is left to each harness.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Adafruit_SSD1306.h"
#include "Arduino.h"
#include "EEPROM.h"
#include "Wire.h"
#include "rgb_lcd.h"

bool g_simVirtualTime = false;
uint32_t g_simVirtualMs = 0;
uint32_t g_simOffsetMs = 0;
int g_simAnalog = 0;
int g_simPins[32] = {};
bool g_simUsbConnected = true;
const char* g_simEepromPath = nullptr;

TwoWire Wire;
SimEeprom EEPROM;

uint32_t millis() {
  if (g_simVirtualTime) {
    return g_simVirtualMs;
  }
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const unsigned long long ms = static_cast<unsigned long long>(ts.tv_sec) * 1000ULL +
                                static_cast<unsigned long long>(ts.tv_nsec) / 1000000ULL;
  return static_cast<uint32_t>(ms) + g_simOffsetMs;
}

uint32_t micros() {
  if (g_simVirtualTime) {
    return g_simVirtualMs * 1000U;
  }
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const unsigned long long us = static_cast<unsigned long long>(ts.tv_sec) * 1000000ULL +
                                static_cast<unsigned long long>(ts.tv_nsec) / 1000ULL;
  return static_cast<uint32_t>(us) + g_simOffsetMs * 1000U;
}

void delay(unsigned long ms) {
  if (g_simVirtualTime) {
    g_simVirtualMs += static_cast<uint32_t>(ms);
  }
}

int analogRead(int) { return g_simAnalog; }

int digitalRead(int pin) { return (pin >= 0 && pin < 32) ? g_simPins[pin] : LOW; }

void pinMode(int, int) {}

// --- EEPROM ------------------------------------------------------------------

void SimEeprom::load() {
  if (loaded_) return;
  memset(bytes_, 0xFF, sizeof bytes_);  // erased flash reads as 0xFF
  if (g_simEepromPath != nullptr) {
    FILE* f = fopen(g_simEepromPath, "rb");
    if (f != nullptr) {
      if (fread(bytes_, 1, sizeof bytes_, f) != sizeof bytes_) {
        memset(bytes_, 0xFF, sizeof bytes_);  // short/corrupt file: treat as erased
      }
      fclose(f);
    }
  }
  loaded_ = true;
}

void SimEeprom::save() {
  if (g_simEepromPath == nullptr) return;
  FILE* f = fopen(g_simEepromPath, "wb");
  if (f != nullptr) {
    if (fwrite(bytes_, 1, sizeof bytes_, f) != sizeof bytes_) {
      fprintf(stderr, "SIM: eeprom write failed\n");
    }
    fclose(f);
  }
}

// --- Grove LCD -----------------------------------------------------------------

void rgb_lcd::begin(uint8_t, uint8_t) {
  clear();
  memset(logged_, 0, sizeof logged_);
}
void rgb_lcd::setRGB(uint8_t r, uint8_t g, uint8_t b) {
  r_ = r;
  g_ = g;
  b_ = b;
}
void rgb_lcd::clear() {
  memset(screen_, ' ', sizeof screen_);
  screen_[0][16] = screen_[1][16] = '\0';
  col_ = row_ = 0;
}
void rgb_lcd::setCursor(uint8_t col, uint8_t row) {
  col_ = col;
  row_ = row < 2 ? row : 1;
}
void rgb_lcd::print(const char* text) {
  for (; *text != '\0'; ++text) {
    if (col_ < 16) {
      screen_[row_][col_++] = *text;
    } else {
      ++overflow_;
    }
  }
}
void rgb_lcd::print(int value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%d", value);
  print(buf);
}
void rgb_lcd::print(unsigned int value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%u", value);
  print(buf);
}
void rgb_lcd::print(long value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%ld", value);
  print(buf);
}
void rgb_lcd::print(unsigned long value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%lu", value);
  print(buf);
}
void rgb_lcd::flushToLog() {
  if (memcmp(screen_, logged_, sizeof screen_) == 0 && r_ == loggedR_ && g_ == loggedG_ &&
      b_ == loggedB_) {
    return;
  }
  memcpy(logged_, screen_, sizeof screen_);
  loggedR_ = r_;
  loggedG_ = g_;
  loggedB_ = b_;
  fprintf(stderr, "LCD [%3u,%3u,%3u] |%s|%s|\n", r_, g_, b_, screen_[0], screen_[1]);
}

// --- SSD1306 OLED ----------------------------------------------------------------

namespace {
constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr int kKnobBarX = 88;    // the sketch reserves x >= 88 of the header
constexpr int kHeaderHeight = 11;
}  // namespace

Adafruit_SSD1306::Adafruit_SSD1306(int, int, TwoWire*, int) {
  memset(draw_, ' ', sizeof draw_);
  memset(shown_, 0, sizeof shown_);
  for (int r = 0; r < kRows; ++r) draw_[r][kCols] = '\0';
}

bool Adafruit_SSD1306::begin(uint8_t, uint8_t) {
  begun_ = beginResult;
  return beginResult;
}

void Adafruit_SSD1306::clearDisplay() {
  if (!begun_) ++overflow_;  // the real library would write through a null buffer
  for (int r = 0; r < kRows; ++r) {
    memset(draw_[r], ' ', kCols);
    draw_[r][kCols] = '\0';
  }
  markerX_ = -1;
}

void Adafruit_SSD1306::setTextSize(int size) { textSize_ = size; }
void Adafruit_SSD1306::setTextColor(uint16_t) {}
void Adafruit_SSD1306::setTextColor(uint16_t, uint16_t) {}
void Adafruit_SSD1306::setTextWrap(bool) {}
void Adafruit_SSD1306::setCursor(int x, int y) {
  x_ = x;
  y_ = y;
}

void Adafruit_SSD1306::putChar(char c) {
  const int w = 6 * textSize_;
  const int h = 8 * textSize_;
  if (x_ < 0 || y_ < 0 || x_ + w - 1 > kWidth || y_ + h > kHeight) {
    ++overflow_;  // clipped on the real panel (setTextWrap(false))
  } else if (y_ < kHeaderHeight && x_ + w - 1 > kKnobBarX) {
    ++overflow_;  // would draw under the knob bar
  } else {
    draw_[y_ / 8][x_ / 6] = c;
  }
  x_ += w;
}

void Adafruit_SSD1306::print(const char* text) {
  for (; *text != '\0'; ++text) putChar(*text);
}
void Adafruit_SSD1306::print(char c) { putChar(c); }
void Adafruit_SSD1306::print(int value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%d", value);
  print(buf);
}
void Adafruit_SSD1306::print(unsigned int value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%u", value);
  print(buf);
}
void Adafruit_SSD1306::print(long value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%ld", value);
  print(buf);
}
void Adafruit_SSD1306::print(unsigned long value) {
  char buf[24];
  snprintf(buf, sizeof buf, "%lu", value);
  print(buf);
}

void Adafruit_SSD1306::display() {
  if (!begun_) ++overflow_;
  for (int r = 0; r < kRows; ++r) {
    memcpy(shown_[r], draw_[r], sizeof shown_[r]);
    int end = kCols;
    while (end > 0 && shown_[r][end - 1] == ' ') --end;  // trim for easy comparisons
    shown_[r][end] = '\0';
  }
  shownMarkerX_ = markerX_;
  ++displayCount_;
}

void Adafruit_SSD1306::checkBounds(int x, int y, int w, int h) {
  if (!begun_ || x < 0 || y < 0 || w < 0 || h < 0 || x + w > kWidth || y + h > kHeight) {
    ++overflow_;
  }
}

void Adafruit_SSD1306::fillRect(int x, int y, int w, int h, uint16_t) { checkBounds(x, y, w, h); }
void Adafruit_SSD1306::drawRect(int x, int y, int w, int h, uint16_t) { checkBounds(x, y, w, h); }
void Adafruit_SSD1306::drawFastHLine(int x, int y, int w, uint16_t) { checkBounds(x, y, w, 1); }
void Adafruit_SSD1306::drawFastVLine(int x, int y, int h, uint16_t) {
  checkBounds(x, y, 1, h);
  markerX_ = x;
}

const char* Adafruit_SSD1306::shownRow(int row) const {
  return (row >= 0 && row < kRows) ? shown_[row] : "";
}
