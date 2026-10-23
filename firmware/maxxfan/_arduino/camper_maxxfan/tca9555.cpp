#include "tca9555.h"
#include <Wire.h>

// Register pairs (port 0 / port 1), auto-increment within each pair
static const uint8_t REG_INPUT    = 0x00;
static const uint8_t REG_OUTPUT   = 0x02;
static const uint8_t REG_POLARITY = 0x04;
static const uint8_t REG_CONFIG   = 0x06;

bool TCA9555::writeReg16(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  Wire.write((uint8_t)(value & 0xFF));
  Wire.write((uint8_t)(value >> 8));
  return Wire.endTransmission() == 0;
}

bool TCA9555::readReg16(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)_addr, 2) != 2) return false;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  value = (uint16_t)lo | ((uint16_t)hi << 8);
  return true;
}

bool TCA9555::begin(uint8_t addr, uint16_t inputMask, uint16_t initialOutputs) {
  _addr = addr;
  // Set output latches first so pins never glitch when switched to output
  _ok = writeReg16(REG_OUTPUT, initialOutputs) &&
        writeReg16(REG_POLARITY, 0x0000) &&
        writeReg16(REG_CONFIG, inputMask);
  return _ok;
}

bool TCA9555::writeOutputs(uint16_t value) {
  if (!writeReg16(REG_OUTPUT, value)) { _ok = false; return false; }
  return true;
}

bool TCA9555::readInputs(uint16_t& value) {
  if (!readReg16(REG_INPUT, value)) { _ok = false; return false; }
  return true;
}
