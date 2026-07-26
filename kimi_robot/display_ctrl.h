#pragma once

#include <Arduino.h>

void    tftInit();
void    tftText(const String& msg, uint8_t size);
void    tftFill(uint8_t r, uint8_t g, uint8_t b);
void    tftClear();
void    tftSetRotation(uint8_t rot);

uint8_t tftGetR();
uint8_t tftGetG();
uint8_t tftGetB();
uint8_t tftGetRotation();
