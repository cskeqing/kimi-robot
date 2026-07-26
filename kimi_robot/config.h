#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ========== 引脚定义 ==========
#define PIN_LED     48
#define NUM_PIXELS   1

// MG90S 舵机：橙=信号→GPIO4，红=电源→5V(VBUS)，棕=GND（必须与 ESP32 共地）
#define PIN_SERVO    4

// 1.77" SPI TFT, ST7735S（接线见 tools/显示器接线.md）
#define TFT_SCK   12
#define TFT_SDA   11   // MOSI
#define TFT_RES   10
#define TFT_RS     9   // DC
#define TFT_CS    14

// ========== 共享类型 ==========
struct WifiCred { const char* ssid; const char* pass; };

struct CmdMsg { char text[80]; };

// ========== 全局对象（各模块 .cpp 中定义）==========
extern WebServer          server;
extern Adafruit_NeoPixel  pixel;
extern Adafruit_ST7735    tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
extern BLEServer*         bleServer;
extern BLECharacteristic* txChar;
extern QueueHandle_t      cmdQueue;
