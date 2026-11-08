// Host stand-in for the Renesas core's EEPROM library. Backed by a file so a
// simulated "power cycle" (restarting the simulator) keeps the boot counter.
#pragma once

#include <stdint.h>
#include <string.h>

class SimEeprom {
 public:
  static constexpr int kSize = 1024;
  template <typename T>
  T& get(int addr, T& out) {
    load();
    if (addr >= 0 && addr + static_cast<int>(sizeof(T)) <= kSize) {
      memcpy(&out, bytes_ + addr, sizeof(T));
    }
    return out;
  }
  template <typename T>
  const T& put(int addr, const T& value) {
    load();
    if (addr >= 0 && addr + static_cast<int>(sizeof(T)) <= kSize) {
      memcpy(bytes_ + addr, &value, sizeof(T));
      save();
    }
    return value;
  }

 private:
  void load();
  void save();
  uint8_t bytes_[kSize];
  bool loaded_ = false;
};
extern SimEeprom EEPROM;
