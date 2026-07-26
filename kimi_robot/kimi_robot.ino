/*
 * Kimi WiFi + BLE Controller
 * Hardware: ESP32-S3
 *   - WS2812 RGB LED on GPIO48
 *   - MG90S 舵机 on GPIO4（50Hz PWM）
 *   - 1.77" ST7735S TFT（软件 SPI，接线见 tools/显示器接线.md）
 *   - WiFi STA + HTTP server on port 80
 *   - BLE UART GATT server, name 'Kimi-Robot'
 */

#include "config.h"
#include <WiFi.h>
#include "led_ctrl.h"
#include "servo_ctrl.h"
#include "display_ctrl.h"
#include "ble_ctrl.h"
#include "wifi_ctrl.h"
#include "web_server.h"
#include "command.h"
#include "esp_system.h"

QueueHandle_t cmdQueue = nullptr;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=============================");
  Serial.println("  Kimi WiFi + BLE Controller");
  Serial.println("=============================");

  {
    const char* rn;
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:  rn = "上电"; break;
      case ESP_RST_SW:       rn = "软件重启"; break;
      case ESP_RST_PANIC:    rn = "崩溃(PANIC)"; break;
      case ESP_RST_INT_WDT:  rn = "中断看门狗"; break;
      case ESP_RST_TASK_WDT: rn = "任务看门狗"; break;
      case ESP_RST_WDT:      rn = "其它看门狗"; break;
      case ESP_RST_BROWNOUT: rn = "掉电(BROWNOUT)"; break;
      case ESP_RST_DEEPSLEEP:rn = "深睡唤醒"; break;
      default:               rn = "未知"; break;
    }
    Serial.printf("[BOOT] 上次复位原因: %s\n", rn);
  }

  cmdQueue = xQueueCreate(8, sizeof(CmdMsg));

  ledInit();
  servoInit();

  tftInit();
  tftText("Kimi Robot\n\n启动中...", 2);

  wifiInit();
  webInit();

  if (wifiIsConnected()) {
    Serial.println();
    Serial.println("  +-------------------------------------+");
    Serial.println("  |  WiFi: http://" + WiFi.localIP().toString());
    Serial.println("  |  BLE:  nRF Connect -> 'Kimi-Robot'");
    Serial.println("  +-------------------------------------+");
    tftText("WiFi 已连接\n\n" + WiFi.localIP().toString() + "\n\n浏览器打开控制页", 1);
  } else {
    tftText("WiFi 连接失败\n\n请检查热点", 1);
  }

  bleInit();
}

void loop() {
  webHandleClient();

  if (cmdQueue) {
    CmdMsg m;
    while (xQueueReceive(cmdQueue, &m, 0) == pdTRUE) {
      handleCommand(String(m.text));
    }
  }

  ledUpdate();
  delay(2);
}
