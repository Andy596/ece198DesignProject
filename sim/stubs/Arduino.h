// Host stand-in for the parts of the Arduino API the sketch uses.
// Only for the simulator; the real board uses the Renesas core.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

constexpr int A2 = 16;

uint32_t millis();
int analogRead(int pin);

// Serial is backed by a pseudo-terminal (see sim_device.cpp).
class SimSerial {
 public:
  void begin(unsigned long baud);
  int available();
  int read();
  size_t write(const uint8_t* data, size_t len);
};
extern SimSerial Serial;
