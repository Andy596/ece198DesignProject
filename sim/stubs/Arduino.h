// Host stand-in for the parts of the Arduino API the sketches use.
// Only for the simulator; the real board uses the Renesas core.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

constexpr int A0 = 14;
constexpr int A1 = 15;
constexpr int A2 = 16;
constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT = 0;

uint32_t millis();
void delay(unsigned long ms);
int analogRead(int pin);
int digitalRead(int pin);
void pinMode(int pin, int mode);

// Same integer arithmetic as the Arduino core's map().
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// The Arduino core defines constrain() as a macro and min()/max() as templates.
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
template <class T, class L>
auto max(const T& a, const L& b) -> decltype((b < a) ? a : b) {
  return (b < a) ? a : b;
}
template <class T, class L>
auto min(const T& a, const L& b) -> decltype((b < a) ? b : a) {
  return (b < a) ? b : a;
}

// Serial is backed by a pseudo-terminal or an in-memory buffer, depending on
// the harness (sim_device.cpp, sim_ui_device.cpp, test_ui.cpp).
class SimSerial {
 public:
  void begin(unsigned long baud);
  int available();
  int read();
  size_t write(const uint8_t* data, size_t len);
};
extern SimSerial Serial;

// --- Simulator controls (sim_hw.cpp) ----------------------------------------
// Virtual time: millis() returns g_simVirtualMs and delay() advances it.
// Real time: millis() is the host clock plus g_simOffsetMs; delay() is a no-op.
extern bool g_simVirtualTime;
extern uint32_t g_simVirtualMs;
extern uint32_t g_simOffsetMs;
extern int g_simAnalog;     // raw reading returned by analogRead()
extern int g_simPins[32];   // levels returned by digitalRead()
extern const char* g_simEepromPath;  // file backing EEPROM, or nullptr
