#include "display_hw.h"
#include "config.h"
#include "settings.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

namespace {

// ---- panel ---------------------------------------------------------------
Arduino_DataBus* bus = new Arduino_SWSPI(GFX_NOT_DEFINED, LCD_SPI_CS, LCD_SPI_SCK, LCD_SPI_MOSI, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
    LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
    LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
    LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
    LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
    1, 10, 8, 50,       // hsync polarity, front porch, pulse width, back porch
    1, 10, 8, 20,       // vsync polarity, front porch, pulse width, back porch
    0, LCD_PCLK_HZ, false, 0, 0, 0);

#if LCD_INIT_TYPE == 1
#define ST7701_INIT st7701_type1_init_operations
#else
#define ST7701_INIT st7701_type9_init_operations
#endif

Arduino_RGB_Display* gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, rgbpanel, LCD_ROTATION, true,
    bus, GFX_NOT_DEFINED, ST7701_INIT, sizeof(ST7701_INIT));

// ---- backlight / screen timeout -----------------------------------------
bool     awake = true;
uint8_t  currentDuty = 0;
uint32_t lastActivity = 0;
bool     swallowTouch = false;   // first touch after sleep only wakes the screen

void applyBacklight(uint8_t pct) {
  uint8_t duty = (uint8_t)((uint16_t)pct * 255 / 100);
  if (duty == currentDuty) return;
  currentDuty = duty;
  ledcWrite(LCD_BL_PIN, duty);
}

// ---- GT911 touch (minimal driver, polled) -------------------------------
uint8_t gtAddr = 0;

bool gtRead(uint16_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gtAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void gtWrite(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  Wire.endTransmission();
}

bool gtBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_I2C_HZ);
  for (uint8_t a : {0x5D, 0x14}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { gtAddr = a; break; }
  }
  if (!gtAddr) { Serial.println("[touch] GT911 not found"); return false; }
  uint8_t id[4] = {0};
  gtRead(0x8140, id, 4);
  Serial.printf("[touch] GT911 at 0x%02X, id %c%c%c%c\n", gtAddr, id[0], id[1], id[2], id[3]);
  return true;
}

bool lastPressed = false;
int16_t lastX = 0, lastY = 0;

void gtPoll() {
  if (!gtAddr) return;
  uint8_t st;
  if (!gtRead(0x814E, &st, 1)) return;
  if (!(st & 0x80)) return;                // no new data, keep last state
  uint8_t n = st & 0x0F;
  if (n > 0 && n <= 5) {
    uint8_t p[6];
    if (gtRead(0x8150, p, 4)) {
      int16_t x = p[0] | (p[1] << 8);
      int16_t y = p[2] | (p[3] << 8);
#if TOUCH_SWAP_XY
      int16_t tmp = x; x = y; y = tmp;
#endif
#if TOUCH_INVERT_X
      x = LCD_WIDTH - 1 - x;
#endif
#if TOUCH_INVERT_Y
      y = LCD_HEIGHT - 1 - y;
#endif
      // follow the display rotation
      switch (LCD_ROTATION & 3) {
        case 1: { int16_t t = x; x = y; y = LCD_WIDTH - 1 - t; break; }
        case 2: x = LCD_WIDTH - 1 - x; y = LCD_HEIGHT - 1 - y; break;
        case 3: { int16_t t = x; x = LCD_HEIGHT - 1 - y; y = t; break; }
        default: break;
      }
      lastX = constrain(x, 0, LCD_WIDTH - 1);
      lastY = constrain(y, 0, LCD_HEIGHT - 1);
      lastPressed = true;
    }
  } else {
    lastPressed = false;
  }
  gtWrite(0x814E, 0);
}

// ---- LVGL glue -----------------------------------------------------------
lv_display_t* lvDisp = nullptr;
lv_indev_t*   lvTouch = nullptr;

uint32_t tickCb() { return millis(); }

void flushCb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px, w, h);
  lv_display_flush_ready(d);
}

void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  gtPoll();
  if (lastPressed) {
    lastActivity = millis();
    if (!awake) {
      Serial.println("[disp] wake: touch");
      disp::wake();
      swallowTouch = true;
    }
  } else {
    swallowTouch = false;
  }
  if (lastPressed && !swallowTouch) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = lastX;
    data->point.y = lastY;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// The draw buffers go to PSRAM by default: two internal buffers took about 90 kB of
// internal RAM, which mbedTLS needs for the HTTPS handshake (Pushover).
void* allocBuf(size_t bytes) {
#if LV_BUF_IN_PSRAM
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
#else
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  return p;
}

} // namespace

namespace disp {

bool begin() {
  ledcAttach(LCD_BL_PIN, 5000, 8);
  ledcWrite(LCD_BL_PIN, 0);

  if (!gfx->begin()) {
    Serial.println("[disp] gfx->begin() failed");
    return false;
  }
  gfx->fillScreen(RGB565_BLACK);
  gtBegin();

  lv_init();
  lv_tick_set_cb(tickCb);

  int16_t w = gfx->width(), h = gfx->height();
  size_t bufBytes = (size_t)w * LV_BUF_LINES * sizeof(uint16_t);
  void* b1 = allocBuf(bufBytes);
  void* b2 = allocBuf(bufBytes);
  if (!b1) { Serial.println("[disp] no memory for LVGL buffer"); return false; }

  lvDisp = lv_display_create(w, h);
  lv_display_set_color_format(lvDisp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisp, flushCb);
  lv_display_set_buffers(lvDisp, b1, b2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lvTouch = lv_indev_create();
  lv_indev_set_type(lvTouch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvTouch, touchCb);

  lastActivity = millis();
  applyBacklight(settings.brightness);
  return true;
}

void loop() {
  lv_timer_handler();
  if (awake && settings.screenTimeout > 0 &&
      millis() - lastActivity > (uint32_t)settings.screenTimeout * 1000UL) {
    sleep();
  }
}

void setBrightness(uint8_t pct) {
  settings.brightness = constrain(pct, 5, 100);
  if (awake) applyBacklight(settings.brightness);
}

void wake() {
  lastActivity = millis();
  if (awake) return;
  awake = true;
  applyBacklight(settings.brightness);
}

void sleep() {
  if (awake) Serial.printf("[disp] screen dimmed to %u %%\n", settings.dimBrightness);
  awake = false;
  applyBacklight(settings.dimBrightness);
}

bool isAwake() { return awake; }
bool touchOk() { return gtAddr != 0; }

} // namespace disp
