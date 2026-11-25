// Host stand-in for the Wire (I2C) library.
#pragma once

class TwoWire {
 public:
  void begin() {}
};
extern TwoWire Wire;
