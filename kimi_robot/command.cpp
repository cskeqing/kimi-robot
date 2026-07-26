#include "config.h"
#include "command.h"
#include "led_ctrl.h"
#include "servo_ctrl.h"
#include "display_ctrl.h"
#include "ble_ctrl.h"
#include <WiFi.h>
#include "esp_system.h"

String handleCommand(const String& cmd) {
  String upper = cmd;
  upper.toUpperCase();
  upper.trim();

  if (upper == "PING") {
    return "PONG";
  }
  if (upper == "MEM") {
    String j;
    j.reserve(256);
    j += "{\"heap\":{\"free\":" + String(ESP.getFreeHeap()) +
         ",\"min\":" + String(ESP.getMinFreeHeap()) +
         ",\"max_alloc\":" + String(ESP.getMaxAllocHeap()) + "},";
    j += "\"sketch\":{\"used\":" + String(ESP.getSketchSize()) +
         ",\"free\":" + String(ESP.getFreeSketchSpace()) + "},";
    j += "\"chip\":{\"model\":\"" + String(ESP.getChipModel()) +
         "\",\"rev\":" + String(ESP.getChipRevision()) +
         ",\"cores\":" + String(ESP.getChipCores()) +
         ",\"cpu_mhz\":" + String(ESP.getCpuFreqMHz()) + "},";
    j += "\"flash_size\":" + String(ESP.getFlashChipSize()) +
         ",\"psram_size\":" + String(ESP.getPsramSize()) +
         ",\"free_psram\":" + String(ESP.getFreePsram()) + "}";
    return j;
  }
  if (upper == "STATUS") {
    String j;
    j.reserve(220);
    j += "{\"wifi\":{\"ssid\":\"" + String(WiFi.SSID()) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"},";
    j += "\"ble\":" + String(bleIsConnected() ? "true" : "false") + ",";
    j += "\"led\":{\"r\":" + String(ledGetR()) + ",\"g\":" + String(ledGetG()) + ",\"b\":" + String(ledGetB()) + "},";
    j += "\"bright\":" + String(ledGetBrightnessPercent()) + ",";
    j += "\"fx\":\"" + String(fxName(ledGetFx())) + "\",";
    j += "\"servo\":{\"angle\":" + String(servoGetAngle()) + ",\"on\":" + String(servoIsAttached() ? "true" : "false") + "},";
    j += "\"tft\":{\"r\":" + String(tftGetR()) + ",\"g\":" + String(tftGetG()) + ",\"b\":" + String(tftGetB()) + ",\"rot\":" + String(tftGetRotation()) + "},";
    j += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
    j += "\"heap_min\":" + String(ESP.getMinFreeHeap()) + "}";
    return j;
  }
  if (upper.startsWith("LED,")) {
    int r, g, b;
    if (sscanf(cmd.c_str(), "LED,%d,%d,%d", &r, &g, &b) == 3) {
      setSolidColor(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
      return "OK LED";
    }
    return "ERR LED format: LED,R,G,B";
  }
  if (upper.startsWith("BRIGHT,")) {
    int p;
    if (sscanf(cmd.c_str(), "BRIGHT,%d", &p) == 1 || sscanf(upper.c_str(), "BRIGHT,%d", &p) == 1) {
      setBrightness(p);
      return "OK BRIGHT";
    }
    return "ERR BRIGHT format: BRIGHT,0-100";
  }
  if (upper.startsWith("FX,")) {
    String v = upper.substring(3); v.trim();
    LedFx nf;
    if      (v == "SOLID")   nf = FX_SOLID;
    else if (v == "BREATHE") nf = FX_BREATHE;
    else if (v == "RAINBOW") nf = FX_RAINBOW;
    else if (v == "BLINK")   nf = FX_BLINK;
    else return "ERR FX format: FX,SOLID|BREATHE|RAINBOW|BLINK";
    setFx(nf);
    return "OK FX=" + v;
  }
  if (upper.startsWith("SERVO,")) {
    String v = upper.substring(6); v.trim();
    if (v == "OFF") { servoRelease(); return "OK SERVO OFF"; }
    int a;
    if (sscanf(v.c_str(), "%d", &a) == 1) {
      servoSetAngle(a);
      return "OK SERVO=" + String(servoGetAngle());
    }
    return "ERR SERVO format: SERVO,0-180|OFF";
  }
  if (upper.startsWith("FILL,")) {
    int r, g, b;
    if (sscanf(cmd.c_str(), "FILL,%d,%d,%d", &r, &g, &b) == 3) {
      tftFill(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
      return "OK FILL";
    }
    return "ERR FILL format: FILL,R,G,B";
  }
  if (upper == "CLEAR") {
    tftClear();
    return "OK CLEAR";
  }
  if (upper.startsWith("ROT,")) {
    int rot;
    if (sscanf(cmd.c_str(), "ROT,%d", &rot) == 1) {
      tftSetRotation(rot);
      return "OK ROT=" + String(tftGetRotation());
    }
    return "ERR ROT format: ROT,0-3";
  }
  if (upper.startsWith("TEXT,")) {
    String rest = cmd.substring(5);
    uint8_t sz = 2;
    int comma = rest.indexOf(',');
    String msg = rest;
    if (comma > 0) {
      String szStr = rest.substring(0, comma);
      bool numeric = szStr.length() > 0;
      for (unsigned int i = 0; i < szStr.length(); i++)
        if (!isdigit(szStr[i])) { numeric = false; break; }
      if (numeric) { sz = (uint8_t)szStr.toInt(); msg = rest.substring(comma + 1); }
    }
    tftText(msg, sz);
    return "OK TEXT";
  }
  return "ERR unknown cmd: " + cmd;
}
