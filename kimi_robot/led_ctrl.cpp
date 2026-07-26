#include "config.h"
#include "led_ctrl.h"

Adafruit_NeoPixel pixel(NUM_PIXELS, PIN_LED, NEO_GRB + NEO_KHZ800);

static uint8_t baseR = 0, baseG = 150, baseB = 136;
static uint8_t brightness = 200;
static LedFx   ledFx = FX_SOLID;
static unsigned long lastLedMs = 0;
static const unsigned long LED_INTERVAL_MS = 16;

static void showRGB(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(
    (uint16_t)r * brightness / 255,
    (uint16_t)g * brightness / 255,
    (uint16_t)b * brightness / 255));
  pixel.show();
}

void ledInit() {
  pixel.begin();
  pixel.clear();
  pixel.show();
  setSolidColor(0, 150, 136);
  Serial.println("[LED] GPIO48 WS2812 ready");
}

void setSolidColor(uint8_t r, uint8_t g, uint8_t b) {
  baseR = r; baseG = g; baseB = b;
  ledFx = FX_SOLID;
  showRGB(baseR, baseG, baseB);
  Serial.printf("[LED] solid R=%d G=%d B=%d\n", r, g, b);
}

void setBrightness(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  brightness = (uint8_t)(percent * 255 / 100);
  if (ledFx == FX_SOLID) showRGB(baseR, baseG, baseB);
  Serial.printf("[LED] bright=%d%%\n", percent);
}

void setFx(LedFx fx) {
  ledFx = fx;
  if (ledFx == FX_SOLID) showRGB(baseR, baseG, baseB);
  Serial.printf("[LED] fx=%s\n", fxName(ledFx));
}

void ledUpdate() {
  unsigned long now = millis();
  if (now - lastLedMs < LED_INTERVAL_MS) return;
  lastLedMs = now;

  switch (ledFx) {
    case FX_SOLID:
      break;
    case FX_BREATHE: {
      float phase = (now % 4000) / 4000.0f * 2.0f * PI;
      float f = (sinf(phase) * 0.5f) + 0.5f;
      f = 0.05f + f * 0.95f;
      showRGB((uint8_t)(baseR * f), (uint8_t)(baseG * f), (uint8_t)(baseB * f));
      break;
    }
    case FX_RAINBOW: {
      uint16_t hue = (uint16_t)((now % 5000) * 65535UL / 5000UL);
      uint32_t c = pixel.ColorHSV(hue, 255, brightness);
      pixel.setPixelColor(0, c);
      pixel.show();
      break;
    }
    case FX_BLINK: {
      bool on = ((now / 500) % 2) == 0;
      if (on) showRGB(baseR, baseG, baseB);
      else    showRGB(0, 0, 0);
      break;
    }
  }
}

const char* fxName(LedFx f) {
  switch (f) {
    case FX_BREATHE: return "BREATHE";
    case FX_RAINBOW: return "RAINBOW";
    case FX_BLINK:   return "BLINK";
    default:         return "SOLID";
  }
}

uint8_t ledGetR() { return baseR; }
uint8_t ledGetG() { return baseG; }
uint8_t ledGetB() { return baseB; }
uint8_t ledGetBrightnessPercent() { return (uint8_t)(brightness * 100 / 255); }
LedFx   ledGetFx() { return ledFx; }
