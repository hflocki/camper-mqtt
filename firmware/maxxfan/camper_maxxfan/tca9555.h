#pragma once
#include <Arduino.h>

// Minimal TCA9555 16-bit I/O expander driver (no external library needed).
class TCA9555 {
public:
  // inputMask: bit = 1 -> pin is an input
  bool begin(uint8_t addr, uint16_t inputMask, uint16_t initialOutputs);
  bool writeOutputs(uint16_t value);
  bool readInputs(uint16_t& value);
  bool ok() const { return _ok; }

private:
  bool writeReg16(uint8_t reg, uint16_t value);
  bool readReg16(uint8_t reg, uint16_t& value);
  uint8_t _addr = 0x20;
  bool _ok = false;
};
