#include "fan_control.h"
#include "config.h"
#include "settings.h"
#include "tca9555.h"
#include <Wire.h>

const char* const BUTTON_KEYS[BTN_COUNT]  = {"auto", "direction", "on_off", "faster", "slower", "beep"};
const char* const BUTTON_NAMES[BTN_COUNT] = {"Auto", "Direction", "On/Off", "Faster", "Slower", "Beep"};
const char* const PORT_LABELS[2]          = {"RJ11", "RJ45"};

namespace {

enum CmdType : uint8_t { CMD_PRESS, CMD_LED, CMD_SYNC };
struct Cmd { CmdType type; uint8_t port; uint8_t arg; };

enum SyncStep : uint8_t { SYNC_IDLE, SYNC_PRESS_AUTO, SYNC_SETTLE, SYNC_WAIT_HOLD, SYNC_PRESS_POWER };

QueueHandle_t queue = nullptr;
TCA9555 tca;

uint16_t outputs     = 0;        // logical: 1 = active
uint16_t lastWritten = 0xFFFF;
bool     needWrite   = true;

uint8_t  pulsePin[2]   = {0xFF, 0xFF};
uint32_t pulseEnd[2]   = {0, 0};
uint32_t portFreeAt[2] = {0, 0};

volatile bool holdState[2] = {false, false};
bool     holdCandidate[2]  = {false, false};
uint32_t lastPoll  = 0;
uint32_t lastRetry = 0;

Cmd  pending;
bool hasPending = false;

volatile SyncStep syncStep = SYNC_IDLE;
uint32_t syncT = 0;
uint8_t  syncPort = 1;

fan::ChangeCb changeCb = nullptr;

inline uint8_t pinHold(uint8_t p)                { return p * 8 + 0; }
inline uint8_t pinLed(uint8_t p)                 { return p * 8 + 1; }
inline uint8_t pinButton(uint8_t p, FanButton b) { return p * 8 + 2 + (uint8_t)b; }

uint16_t inputMask() { return (1u << pinHold(0)) | (1u << pinHold(1)); }

uint16_t toPhysical(uint16_t logical) {
#if OUTPUT_ACTIVE_HIGH
  return logical;
#else
  return (uint16_t)~logical;
#endif
}

inline bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

void notify() { if (changeCb) changeCb(); }

void flush() {
  if (!tca.ok()) return;
  if (outputs == lastWritten && !needWrite) return;
  if (tca.writeOutputs(toPhysical(outputs))) { lastWritten = outputs; needWrite = false; }
}

void initExpander() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
  if (tca.begin(TCA9555_ADDR, inputMask(), toPhysical(outputs))) {
    lastWritten = outputs;
    needWrite = false;
    Serial.printf("[fan] TCA9555 found at 0x%02X\n", TCA9555_ADDR);
  } else {
    Serial.printf("[fan] TCA9555 not responding at 0x%02X\n", TCA9555_ADDR);
  }
}

// Starts a press on a port; returns false if that port is still busy.
bool startPress(uint8_t port, FanButton btn) {
  uint32_t now = millis();
  if (pulsePin[port] != 0xFF || !due(now, portFreeAt[port])) return false;
  pulsePin[port] = pinButton(port, btn);
  pulseEnd[port] = now + settings.pulseMs;
  outputs |= (1u << pulsePin[port]);
  flush();
  Serial.printf("[fan] %s: press %s\n", PORT_LABELS[port], BUTTON_NAMES[btn]);
  return true;
}

void handlePulses(uint32_t now) {
  for (uint8_t p = 0; p < 2; p++) {
    if (pulsePin[p] != 0xFF && due(now, pulseEnd[p])) {
      outputs &= ~(1u << pulsePin[p]);
      pulsePin[p] = 0xFF;
      portFreeAt[p] = now + settings.pulseMs;   // gap so presses don't merge
      flush();
    }
  }
}

void pollInputs(uint32_t now) {
  if (now - lastPoll < INPUT_POLL_MS || !tca.ok()) return;
  lastPoll = now;
  uint16_t in;
  if (!tca.readInputs(in)) return;
  bool changed = false;
  for (uint8_t p = 0; p < 2; p++) {
    bool raw = (in >> pinHold(p)) & 1;
#if !HOLD_ACTIVE_HIGH
    raw = !raw;
#endif
    // simple 2-sample debounce
    if (raw == holdCandidate[p] && raw != holdState[p]) {
      holdState[p] = raw;
      changed = true;
      Serial.printf("[fan] %s: auto hold %s\n", PORT_LABELS[p], raw ? "ON" : "OFF");
    }
    holdCandidate[p] = raw;
  }
  if (changed) notify();
}

void handleSync(uint32_t now) {
  switch (syncStep) {
    case SYNC_IDLE: break;
    case SYNC_PRESS_AUTO:
      if (startPress(syncPort, BTN_AUTO)) { syncT = now; syncStep = SYNC_SETTLE; }
      break;
    case SYNC_SETTLE:
      if (now - syncT >= SYNC_AUTO_SETTLE_MS) { syncT = now; syncStep = SYNC_WAIT_HOLD; }
      break;
    case SYNC_WAIT_HOLD:
      if (holdState[syncPort] || now - syncT >= SYNC_HOLD_TIMEOUT_MS) {
        if (!holdState[syncPort]) Serial.println("[fan] sync: auto hold timeout, continuing");
        syncStep = SYNC_PRESS_POWER;
      }
      break;
    case SYNC_PRESS_POWER:
      if (startPress(syncPort, BTN_ON_OFF)) {
        syncStep = SYNC_IDLE;
        Serial.println("[fan] sync: done");
        notify();
      }
      break;
  }
}

void handleQueue() {
  if (!hasPending && xQueueReceive(queue, &pending, 0) == pdTRUE) hasPending = true;
  if (!hasPending) return;

  const Cmd& c = pending;
  if (c.port > 1 || !settings.portEnabled[c.port]) { hasPending = false; return; }

  switch (c.type) {
    case CMD_PRESS:
      if (startPress(c.port, (FanButton)c.arg)) hasPending = false;   // else retry next loop
      break;
    case CMD_LED:
      if (c.arg) outputs |= (1u << pinLed(c.port));
      else       outputs &= ~(1u << pinLed(c.port));
      flush();
      hasPending = false;
      notify();
      break;
    case CMD_SYNC:
      if (syncStep == SYNC_IDLE) {
        syncPort = c.port;
        syncStep = SYNC_PRESS_AUTO;
        Serial.printf("[fan] sync to OFF on %s\n", PORT_LABELS[syncPort]);
        notify();
      }
      hasPending = false;
      break;
  }
}

bool enqueue(const Cmd& c) {
  return queue && xQueueSend(queue, &c, 0) == pdTRUE;
}

} // namespace

namespace fan {

void begin() {
  queue = xQueueCreate(40, sizeof(Cmd));
  initExpander();
}

void loop() {
  uint32_t now = millis();
  if (!tca.ok()) {
    if (now - lastRetry > 5000) { lastRetry = now; initExpander(); needWrite = true; }
    return;
  }
  handlePulses(now);
  pollInputs(now);
  handleSync(now);
  handleQueue();
  flush();
}

bool requestPress(uint8_t port, FanButton btn) {
  if (port > 1 || btn >= BTN_COUNT) return false;
  return enqueue({CMD_PRESS, port, (uint8_t)btn});
}

bool requestLed(uint8_t port, bool on) {
  if (port > 1) return false;
  return enqueue({CMD_LED, port, (uint8_t)(on ? 1 : 0)});
}

bool requestSyncOff() {
  return enqueue({CMD_SYNC, settings.syncPort, 0});
}

// "Slower" on the lowest speed closes the lid, so a speed is set from the top:
// 10x faster (stops at 10), then down to the wanted level. The lid stays open.
bool requestSpeed(uint8_t port, uint8_t level) {
  if (port > 1 || level < 1 || level > 10) return false;
  if (uxQueueSpacesAvailable(queue) < 10u + (10u - level)) return false;
  for (uint8_t i = 0; i < 10; i++)        enqueue({CMD_PRESS, port, (uint8_t)BTN_FASTER});
  for (uint8_t i = level; i < 10; i++)    enqueue({CMD_PRESS, port, (uint8_t)BTN_SLOWER});
  Serial.printf("[fan] %s: set speed %u\n", PORT_LABELS[port], level);
  return true;
}

// The On/Off key only opens the lid; it closes after one more "slower" on the lowest speed.
// CLOSE_PRESSES covers the way down from speed 10 plus that final press.
bool requestClose(uint8_t port) {
  if (port > 1) return false;
  if (uxQueueSpacesAvailable(queue) < CLOSE_PRESSES) return false;
  for (uint8_t i = 0; i < CLOSE_PRESSES; i++) enqueue({CMD_PRESS, port, (uint8_t)BTN_SLOWER});
  Serial.printf("[fan] %s: close lid\n", PORT_LABELS[port]);
  return true;
}

bool autoHold(uint8_t port)  { return port < 2 && holdState[port]; }
bool led(uint8_t port)       { return port < 2 && (outputs & (1u << pinLed(port))); }
bool expanderOk()            { return tca.ok(); }
bool syncRunning()           { return syncStep != SYNC_IDLE; }
void onChange(ChangeCb cb)   { changeCb = cb; }

FanButton buttonFromKey(const String& key) {
  String k = key;
  k.trim();
  k.toLowerCase();
  if (k == "onoff" || k == "power") k = "on_off";
  for (uint8_t i = 0; i < BTN_COUNT; i++)
    if (k == BUTTON_KEYS[i]) return (FanButton)i;
  return BTN_COUNT;
}

} // namespace fan
