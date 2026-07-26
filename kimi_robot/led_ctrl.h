#pragma once

#include <Arduino.h>

enum LedFx : uint8_t { FX_SOLID = 0, FX_BREATHE = 1, FX_RAINBOW = 2, FX_BLINK = 3 };

void        ledInit();
void        ledUpdate();
void        setSolidColor(uint8_t r, uint8_t g, uint8_t b);
void        setBrightness(uint8_t percent);
void        setFx(LedFx fx);
const char* fxName(LedFx f);

uint8_t     ledGetR();
uint8_t     ledGetG();
uint8_t     ledGetB();
uint8_t     ledGetBrightnessPercent();
LedFx       ledGetFx();
