/*
 * Kimi WiFi + BLE Controller (精简版 · 灯光控制器)
 * Hardware: ESP32-S3
 *   - WS2812 RGB LED on GPIO48   （状态指示灯 + 灯光控制）
 *   - WiFi STA + HTTP server on port 80   （控制网页）
 *   - BLE UART GATT server, name 'Kimi-Robot'   （蓝牙命令）
 *
 * WiFi: 扫描周围网络，按列表优先级连接第一个可用网络。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_system.h"

// ========== WiFi 配置（按优先级从上到下）==========
// 复制 wifi_secrets.example.h 为 wifi_secrets.h，并填写本地热点信息。
// wifi_secrets.h 已被 Git 忽略，不会上传真实密码。
struct WifiCred { const char* ssid; const char* pass; };
#include "wifi_secrets.h"
static const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

// ========== 引脚定义 ==========
#define PIN_LED     48   // WS2812
#define NUM_PIXELS   1

// ========== 显示屏引脚（1.77" SPI TFT, ST7735S）==========
// 接线见 tools/显示器接线.md
#define TFT_SCK   12
#define TFT_SDA   11   // MOSI
#define TFT_RES   10
#define TFT_RS     9   // DC
#define TFT_CS    14

// ========== BLE UART Service ==========
static BLEUUID  SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID  CHAR_RX_UUID ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // 写
static BLEUUID  CHAR_TX_UUID ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // 通知

// ========== 全局对象 ==========
WebServer          server(80);
Adafruit_NeoPixel  pixel(NUM_PIXELS, PIN_LED, NEO_GRB + NEO_KHZ800);
// 硬件 SPI（FSPI）：ESP32-S3 通过 GPIO 矩阵把 SCK/MOSI 映射到接线所用引脚，
// 由片上 SPI 外设产生时钟，比软件 bit-bang 更稳更快。
SPIClass           tftSPI(FSPI);
Adafruit_ST7735    tft(&tftSPI, TFT_CS, TFT_RS, TFT_RES);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;   // 在 Adafruit_GFX 之上渲染 UTF-8 中文字体
BLEServer*         bleServer = nullptr;
BLECharacteristic* txChar    = nullptr;
bool               bleConnected = false;

// 命令队列：BLE 回调运行在蓝牙协议栈任务里，绝不能在那个任务里直接碰 SPI/WS2812
// （会和 loop 任务抢硬件，导致数据竞争 → 崩溃白屏）。BLE 只把命令入队，
// 统一由 loop 任务取出执行，保证所有硬件访问都在同一个任务里串行发生。
struct CmdMsg { char text[80]; };
static QueueHandle_t cmdQueue = nullptr;

// ========== 灯光状态 ==========
enum LedFx : uint8_t { FX_SOLID = 0, FX_BREATHE = 1, FX_RAINBOW = 2, FX_BLINK = 3 };
static uint8_t baseR = 0, baseG = 150, baseB = 136;  // 用户选定的基色（未乘亮度）
static uint8_t brightness = 200;                     // 0-255 全局亮度
static LedFx   ledFx = FX_SOLID;                     // 当前灯效
static unsigned long lastLedMs = 0;
static const unsigned long LED_INTERVAL_MS = 16;     // ~60fps 动画节流

// 把一个已经算好的 RGB（未乘亮度）按全局亮度输出到灯
static void showRGB(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(
    (uint16_t)r * brightness / 255,
    (uint16_t)g * brightness / 255,
    (uint16_t)b * brightness / 255));
  pixel.show();
}

// 设置基色并切回常亮
static void setSolidColor(uint8_t r, uint8_t g, uint8_t b) {
  baseR = r; baseG = g; baseB = b;
  ledFx = FX_SOLID;
  showRGB(baseR, baseG, baseB);
  Serial.printf("[LED] solid R=%d G=%d B=%d\n", r, g, b);
}

// 非阻塞灯效更新，loop() 每次调用一次，内部自行节流
static void ledUpdate() {
  unsigned long now = millis();
  if (now - lastLedMs < LED_INTERVAL_MS) return;
  lastLedMs = now;

  switch (ledFx) {
    case FX_SOLID:
      // 常亮：颜色/亮度变化时已即时输出，这里无需每帧刷新
      break;
    case FX_BREATHE: {
      // 呼吸：正弦亮度包络，周期约 4 秒
      float phase = (now % 4000) / 4000.0f * 2.0f * PI;
      float f = (sinf(phase) * 0.5f) + 0.5f;         // 0..1
      f = 0.05f + f * 0.95f;                          // 保底一点微光
      showRGB((uint8_t)(baseR * f), (uint8_t)(baseG * f), (uint8_t)(baseB * f));
      break;
    }
    case FX_RAINBOW: {
      // 彩虹：色相随时间循环，周期约 5 秒
      uint16_t hue = (uint16_t)((now % 5000) * 65535UL / 5000UL);
      uint32_t c = pixel.ColorHSV(hue, 255, brightness);
      pixel.setPixelColor(0, c);
      pixel.show();
      break;
    }
    case FX_BLINK: {
      // 闪烁：500ms 亮 / 500ms 灭
      bool on = ((now / 500) % 2) == 0;
      if (on) showRGB(baseR, baseG, baseB);
      else    showRGB(0, 0, 0);
      break;
    }
  }
}

static const char* fxName(LedFx f) {
  switch (f) {
    case FX_BREATHE: return "BREATHE";
    case FX_RAINBOW: return "RAINBOW";
    case FX_BLINK:   return "BLINK";
    default:         return "SOLID";
  }
}

// ========== 显示屏 ==========
// 1.77" ST7735S，逻辑分辨率 128x160。
static const int16_t TFT_W = 128;
static const int16_t TFT_H = 160;
static uint8_t  scrR = 0, scrG = 0, scrB = 0;   // 记录当前底色，供 STATUS 回显
static uint8_t  scrRot = 0;                     // 当前旋转 0..3

static void tftInit() {
  // FSPI.begin(sck, miso, mosi, ss)；显示屏无 MISO，传 -1
  tftSPI.begin(TFT_SCK, -1, TFT_SDA, TFT_CS);
  tft.initR(INITR_BLACKTAB);      // 1.77" 128x160 面板常用初始化序列
  tft.setRotation(scrRot);
  tft.fillScreen(ST77XX_BLACK);
  // U8g2 中文渲染引擎挂到 tft 上
  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);        // 透明背景（只画字，不覆盖底色）
  u8g2Fonts.setFontDirection(0);
  Serial.println("[TFT] ST7735S ready (128x160, HW SPI, U8g2 中文)");
}

// 按 UTF-8 逐字符绘制，支持 \n 换行和到达屏幕右边界时自动折行。
// u8g2Fonts.print 本身不换行，所以这里手动处理。
static void tftDrawWrapped(const String& msg, int lineH) {
  int screenW = tft.width();
  int x = 2, y = lineH;
  int i = 0, n = msg.length();
  while (i < n) {
    char c = msg[i];
    if (c == '\n') { y += lineH; x = 2; i++; continue; }
    // 判断这个 UTF-8 字符占几个字节
    uint8_t u = (uint8_t)c;
    int len = 1;
    if      (u >= 0xF0) len = 4;
    else if (u >= 0xE0) len = 3;
    else if (u >= 0xC0) len = 2;
    if (i + len > n) len = 1;               // 防越界
    String ch = msg.substring(i, i + len);
    int w = u8g2Fonts.getUTF8Width(ch.c_str());
    if (x + w > screenW) { y += lineH; x = 2; }  // 到边界折行
    if (y > tft.height() + lineH) break;         // 超出屏幕底部就停
    u8g2Fonts.setCursor(x, y);
    u8g2Fonts.print(ch);
    x += w;
    i += len;
  }
}

// 用纯色填满屏幕
static void tftFill(uint8_t r, uint8_t g, uint8_t b) {
  scrR = r; scrG = g; scrB = b;
  tft.fillScreen(tft.color565(r, g, b));
  Serial.printf("[TFT] fill #%02X%02X%02X\n", r, g, b);
}

// 清屏（黑）
static void tftClear() {
  tftFill(0, 0, 0);
}

// 显示一段文字（黑底白字，支持中文，自动换行）
// size：1=小(12px)，2/3=大(16px)。U8g2 位图字体尺寸固定，故只有两档。
static void tftText(const String& msg, uint8_t size) {
  size = constrain(size, 1, 3);
  tft.fillScreen(ST77XX_BLACK);
  scrR = scrG = scrB = 0;
  int lineH;
  if (size == 1) { u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312); lineH = 15; }
  else           { u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312); lineH = 20; }
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  tftDrawWrapped(msg, lineH);
  Serial.printf("[TFT] text(%d): %s\n", size, msg.c_str());
}

// ========== BLE 回调 ==========
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true;  Serial.println("[BLE] Client connected"); }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected");
    s->getAdvertising()->start();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = c->getValue();
    if (s.length() == 0) return;
    s.trim();
    Serial.printf("[BLE RX] %s\n", s.c_str());
    // 不在 BLE 任务里执行硬件操作，只入队，交给 loop 任务处理
    if (cmdQueue) {
      CmdMsg m;
      strlcpy(m.text, s.c_str(), sizeof(m.text));
      xQueueSend(cmdQueue, &m, 0);
    }
  }
};

// ========== 命令处理（BLE 和 HTTP 共用）==========
// 命令格式（不区分大小写）：
//   LED,R,G,B        设置颜色并常亮，例 LED,255,0,0
//   BRIGHT,N         亮度 0-100，例 BRIGHT,80
//   FX,NAME          灯效 SOLID|BREATHE|RAINBOW|BLINK
//   STATUS           返回状态 JSON
//   MEM              返回内存/芯片信息 JSON
//   PING             返回 PONG
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
    j += "\"ble\":" + String(bleConnected ? "true" : "false") + ",";
    j += "\"led\":{\"r\":" + String(baseR) + ",\"g\":" + String(baseG) + ",\"b\":" + String(baseB) + "},";
    j += "\"bright\":" + String((int)(brightness * 100 / 255)) + ",";
    j += "\"fx\":\"" + String(fxName(ledFx)) + "\",";
    j += "\"tft\":{\"r\":" + String(scrR) + ",\"g\":" + String(scrG) + ",\"b\":" + String(scrB) + ",\"rot\":" + String(scrRot) + "},";
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
      p = constrain(p, 0, 100);
      brightness = (uint8_t)(p * 255 / 100);
      if (ledFx == FX_SOLID) showRGB(baseR, baseG, baseB);
      Serial.printf("[LED] bright=%d%%\n", p);
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
    ledFx = nf;
    if (ledFx == FX_SOLID) showRGB(baseR, baseG, baseB);
    Serial.printf("[LED] fx=%s\n", fxName(ledFx));
    return "OK FX=" + v;
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
      scrRot = (uint8_t)(rot & 3);
      tft.setRotation(scrRot);
      tft.fillScreen(tft.color565(scrR, scrG, scrB));
      return "OK ROT=" + String(scrRot);
    }
    return "ERR ROT format: ROT,0-3";
  }
  if (upper.startsWith("TEXT,")) {
    // 格式：TEXT,<size>,<message>；size 可省略（默认 2）。
    // 只按前两个逗号切分，message 内部可以含逗号。
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

// ========== HTTP 路由 ==========
static void httpRoot() {
  String ip = WiFi.localIP().toString();
  String html = String(
    "<!doctype html><html lang='zh'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
    "<meta name='theme-color' content='#0b1120'>"
    "<title>灯光控制</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}"
    "html,body{height:100%}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC',sans-serif;"
    "background:radial-gradient(ellipse at top,#1e293b,#0b1120 60%);color:#e2e8f0;"
    "min-height:100vh;padding:16px;max-width:520px;margin:0 auto;overflow-x:hidden}"
    ".hdr{text-align:center;padding:14px 0 8px}"
    ".hdr h1{font-size:24px;font-weight:700;background:linear-gradient(135deg,#22d3ee,#a78bfa,#f472b6);"
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;letter-spacing:.5px}"
    ".hdr .sub{font-size:12px;color:#64748b;margin-top:6px;display:flex;align-items:center;justify-content:center;gap:8px}"
    ".dot{width:6px;height:6px;border-radius:50%;background:#22c55e;box-shadow:0 0 8px #22c55e;animation:pulse 2s infinite}"
    "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}"
    ".hdr .ip{font-size:11px;color:#475569;margin-top:4px;font-family:ui-monospace,monospace}"
    ".card{background:rgba(30,41,59,.55);border:1px solid rgba(148,163,184,.08);"
    "border-radius:20px;padding:18px;margin-bottom:14px;backdrop-filter:blur(16px);"
    "box-shadow:0 8px 32px rgba(0,0,0,.25),inset 0 1px 0 rgba(255,255,255,.04)}"
    ".card h3{font-size:12px;font-weight:600;color:#94a3b8;margin-bottom:14px;"
    "text-transform:uppercase;letter-spacing:1.5px;display:flex;align-items:center;gap:8px}"
    ".card h3::before{content:'';width:4px;height:14px;background:linear-gradient(180deg,#22d3ee,#a78bfa);border-radius:2px}"
    ".led-preview{width:100%;height:120px;border-radius:16px;margin-bottom:16px;"
    "background:#0f172a;position:relative;overflow:hidden;border:1px solid rgba(148,163,184,.1)}"
    ".led-preview .glow{position:absolute;inset:0;background:radial-gradient(ellipse at center,var(--c,rgba(34,211,238,.4)),transparent 70%);"
    "transition:background .2s}"
    ".led-preview .val{position:absolute;bottom:10px;right:12px;font-family:ui-monospace,monospace;"
    "font-size:11px;color:#94a3b8;letter-spacing:.5px}"
    ".rgb-wrap{display:flex;flex-direction:column;gap:12px}"
    ".rgb-row{display:flex;align-items:center;gap:10px}"
    ".rgb-lab{width:22px;font-size:11px;font-weight:700;text-align:center;border-radius:6px;"
    "padding:4px 0;color:#fff;font-family:ui-monospace,monospace}"
    ".rgb-lab.r{background:linear-gradient(135deg,#ef4444,#dc2626)}"
    ".rgb-lab.g{background:linear-gradient(135deg,#22c55e,#16a34a)}"
    ".rgb-lab.b{background:linear-gradient(135deg,#3b82f6,#2563eb)}"
    ".rgb-lab.w{background:linear-gradient(135deg,#f59e0b,#d97706)}"
    ".rgb-num{width:44px;text-align:right;font-family:ui-monospace,monospace;font-size:13px;color:#cbd5e1;font-weight:600}"
    "input[type=range].rgb{-webkit-appearance:none;flex:1;height:28px;border-radius:14px;"
    "outline:none;background:transparent;position:relative}"
    "input[type=range].rgb::-webkit-slider-runnable-track{height:6px;border-radius:3px;"
    "background:linear-gradient(90deg,#1e293b 0%,var(--tc) 100%)}"
    "input[type=range].rgb::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;"
    "border-radius:50%;background:#fff;cursor:pointer;margin-top:-8px;"
    "box-shadow:0 2px 10px rgba(0,0,0,.4),0 0 0 3px rgba(255,255,255,.1)}"
    ".quick-colors{display:grid;grid-template-columns:repeat(6,1fr);gap:8px;margin-top:14px}"
    ".qc{height:32px;border-radius:10px;border:none;cursor:pointer;transition:transform .12s,box-shadow .2s;"
    "background:var(--c);box-shadow:0 2px 8px rgba(0,0,0,.3)}"
    ".qc:active{transform:scale(.9)}"
    ".qc.off{background:#334155;border:1px solid #475569;color:#cbd5e1;font-size:12px}"
    ".fx-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}"
    ".fx{padding:14px 10px;border-radius:12px;border:1px solid rgba(148,163,184,.12);background:#0f172a;"
    "color:#cbd5e1;font-size:14px;font-weight:600;cursor:pointer;transition:all .15s;display:flex;"
    "align-items:center;justify-content:center;gap:8px}"
    ".fx:active{transform:scale(.96)}"
    ".fx.on{border-color:#22d3ee;background:linear-gradient(135deg,rgba(34,211,238,.18),rgba(167,139,250,.14));color:#fff;"
    "box-shadow:0 0 0 1px rgba(34,211,238,.4),0 4px 16px rgba(34,211,238,.15)}"
    ".tft-text input{width:100%;padding:12px 14px;border-radius:12px;border:1px solid rgba(148,163,184,.15);"
    "background:#0f172a;color:#e2e8f0;font-size:15px;outline:none;margin-bottom:10px}"
    ".tft-text input:focus{border-color:#22d3ee}"
    ".tft-row{display:flex;gap:10px}"
    ".tft-row select{background:#0f172a;color:#cbd5e1;border:1px solid rgba(148,163,184,.15);border-radius:12px;"
    "padding:0 12px;font-size:14px;outline:none}"
    ".tbtn{flex:1;padding:12px;border-radius:12px;border:1px solid rgba(148,163,184,.15);background:#0f172a;"
    "color:#cbd5e1;font-size:14px;font-weight:600;cursor:pointer;transition:all .15s}"
    ".tbtn:active{transform:scale(.97)}"
    ".tbtn.send{background:linear-gradient(135deg,#22d3ee,#a78bfa);color:#0b1120;border:none}"
    ".tft-fill{display:grid;grid-template-columns:repeat(6,1fr);gap:8px;margin:14px 0}"
    ".sc{height:36px;border-radius:10px;border:1px solid rgba(148,163,184,.12);cursor:pointer;background:var(--c);"
    "box-shadow:0 2px 8px rgba(0,0,0,.3);transition:transform .12s}"
    ".sc:active{transform:scale(.9)}"
    ".tft-row2{display:flex;gap:10px}"
    ".status-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:12px}"
    ".status-item{background:#0f172a;border-radius:10px;padding:10px 12px;border:1px solid rgba(148,163,184,.06)}"
    ".status-item .k{font-size:10px;color:#64748b;text-transform:uppercase;letter-spacing:1px;margin-bottom:4px}"
    ".status-item .v{font-family:ui-monospace,monospace;font-size:13px;color:#e2e8f0;font-weight:500;word-break:break-all}"
    ".status-item .v.good{color:#22c55e}"
    ".status-item .v.warn{color:#f59e0b}"
    "</style></head><body>"
    "<div class='hdr'>"
    "<h1>灯光控制</h1>"
    "<div class='sub'><span class='dot'></span><span>Kimi Robot 在线</span></div>"
    "<div class='ip'>") + ip + String("</div>"
    "</div>"

    "<div class='card'>"
    "<h3>颜色</h3>"
    "<div class='led-preview' id='lp'><div class='glow' id='lg'></div><div class='val' id='lv'>RGB(0,150,136)</div></div>"
    "<div class='rgb-wrap'>"
    "<div class='rgb-row'><span class='rgb-lab r'>R</span>"
    "<input type='range' class='rgb' id='r' min='0' max='255' value='0' style='--tc:#ef4444' oninput='updLED()'>"
    "<span class='rgb-num' id='rv'>0</span></div>"
    "<div class='rgb-row'><span class='rgb-lab g'>G</span>"
    "<input type='range' class='rgb' id='g' min='0' max='255' value='150' style='--tc:#22c55e' oninput='updLED()'>"
    "<span class='rgb-num' id='gv'>150</span></div>"
    "<div class='rgb-row'><span class='rgb-lab b'>B</span>"
    "<input type='range' class='rgb' id='b' min='0' max='255' value='136' style='--tc:#3b82f6' oninput='updLED()'>"
    "<span class='rgb-num' id='bv'>136</span></div>"
    "</div>"
    "<div class='quick-colors'>"
    "<button class='qc' style='--c:#ef4444' onclick=\"setLED(255,0,0)\"></button>"
    "<button class='qc' style='--c:#f97316' onclick=\"setLED(249,115,22)\"></button>"
    "<button class='qc' style='--c:#eab308' onclick=\"setLED(234,179,8)\"></button>"
    "<button class='qc' style='--c:#22c55e' onclick=\"setLED(34,197,94)\"></button>"
    "<button class='qc' style='--c:#3b82f6' onclick=\"setLED(59,130,246)\"></button>"
    "<button class='qc' style='--c:#a855f7' onclick=\"setLED(168,85,247)\"></button>"
    "<button class='qc' style='--c:#ec4899' onclick=\"setLED(236,72,153)\"></button>"
    "<button class='qc' style='--c:#14b8a6' onclick=\"setLED(20,184,166)\"></button>"
    "<button class='qc' style='--c:#f8fafc' onclick=\"setLED(255,255,255)\"></button>"
    "<button class='qc off' onclick=\"setLED(0,0,0)\">关</button>"
    "</div>"
    "</div>"

    "<div class='card'>"
    "<h3>亮度</h3>"
    "<div class='rgb-row'><span class='rgb-lab w'>%</span>"
    "<input type='range' class='rgb' id='br' min='0' max='100' value='78' style='--tc:#f59e0b' oninput='updBright()'>"
    "<span class='rgb-num' id='brv'>78</span></div>"
    "</div>"

    "<div class='card'>"
    "<h3>灯效</h3>"
    "<div class='fx-grid'>"
    "<button class='fx on' id='fx-SOLID' onclick=\"setFx('SOLID')\">● 常亮</button>"
    "<button class='fx' id='fx-BREATHE' onclick=\"setFx('BREATHE')\">◐ 呼吸</button>"
    "<button class='fx' id='fx-RAINBOW' onclick=\"setFx('RAINBOW')\">◈ 彩虹</button>"
    "<button class='fx' id='fx-BLINK' onclick=\"setFx('BLINK')\">◇ 闪烁</button>"
    "</div>"
    "</div>"

    "<div class='card'>"
    "<h3>显示屏</h3>"
    "<div class='tft-text'>"
    "<input type='text' id='tt' maxlength='120' placeholder='输入要显示的文字...' value='你好，Kimi 机器人'>"
    "<div class='tft-row'>"
    "<select id='tsz'><option value='1'>小</option><option value='2' selected>中</option><option value='3'>大</option></select>"
    "<button class='tbtn send' onclick='sendText()'>显示文字</button>"
    "</div>"
    "</div>"
    "<div class='tft-fill'>"
    "<button class='sc' style='--c:#ef4444' onclick='fill(255,0,0)'></button>"
    "<button class='sc' style='--c:#22c55e' onclick='fill(34,197,94)'></button>"
    "<button class='sc' style='--c:#3b82f6' onclick='fill(59,130,246)'></button>"
    "<button class='sc' style='--c:#eab308' onclick='fill(234,179,8)'></button>"
    "<button class='sc' style='--c:#a855f7' onclick='fill(168,85,247)'></button>"
    "<button class='sc' style='--c:#f8fafc' onclick='fill(255,255,255)'></button>"
    "</div>"
    "<div class='tft-row2'>"
    "<button class='tbtn' onclick='rotate()'>旋转 90°</button>"
    "<button class='tbtn' onclick='clr()'>清屏</button>"
    "</div>"
    "</div>"

    "<div class='card'>"
    "<h3>设备状态</h3>"
    "<div class='status-grid'>"
    "<div class='status-item'><div class='k'>WiFi</div><div class='v' id='sw'>--</div></div>"
    "<div class='status-item'><div class='k'>IP</div><div class='v' id='si'>--</div></div>"
    "<div class='status-item'><div class='k'>BLE</div><div class='v' id='sb'>--</div></div>"
    "<div class='status-item'><div class='k'>可用内存</div><div class='v' id='sm'>--</div></div>"
    "</div>"
    "</div>"

    "<script>"
    "var ledTimer=null,brTimer=null;"
    "function $(id){return document.getElementById(id)}"
    "function get(u){return fetch(u).then(function(r){return r.text()})}"
    "function updLED(){"
    "var r=+$('r').value,g=+$('g').value,b=+$('b').value;"
    "$('rv').textContent=r;$('gv').textContent=g;$('bv').textContent=b;"
    "var c='rgb('+r+','+g+','+b+')';"
    "$('lg').style.background='radial-gradient(ellipse at center,'+c+',transparent 70%)';$('lv').textContent='RGB('+r+','+g+','+b+')';"
    "setFxUI('SOLID');"
    "clearTimeout(ledTimer);ledTimer=setTimeout(function(){get('/cmd?c='+encodeURIComponent('LED,'+r+','+g+','+b))},80)"
    "}"
    "function setLED(r,g,b){$('r').value=r;$('g').value=g;$('b').value=b;updLED()}"
    "function updBright(){var v=+$('br').value;$('brv').textContent=v;"
    "clearTimeout(brTimer);brTimer=setTimeout(function(){get('/cmd?c=BRIGHT,'+v)},80)}"
    "function setFxUI(f){['SOLID','BREATHE','RAINBOW','BLINK'].forEach(function(x){"
    "$('fx-'+x).className='fx'+(x===f?' on':'')})}"
    "function setFx(f){setFxUI(f);get('/cmd?c=FX,'+f)}"
    "var tftRot=0;"
    "function sendText(){var m=$('tt').value||' ';var s=$('tsz').value;get('/cmd?c='+encodeURIComponent('TEXT,'+s+','+m))}"
    "function fill(r,g,b){get('/cmd?c='+encodeURIComponent('FILL,'+r+','+g+','+b))}"
    "function clr(){get('/cmd?c=CLEAR')}"
    "function rotate(){tftRot=(tftRot+1)%4;get('/cmd?c=ROT,'+tftRot)}"
    "function updStatus(d){"
    "$('sw').textContent=d.wifi.ssid||'--';$('sw').className='v '+(d.wifi.ssid?'good':'warn');"
    "$('si').textContent=d.wifi.ip||'--';"
    "$('sb').textContent=d.ble?'已连接':'待机';$('sb').className='v '+(d.ble?'good':'warn');"
    "if(d.heap_free!=null){$('sm').textContent=Math.round(d.heap_free/1024)+' KB'}"
    "}"
    "function syncFromDevice(d){"
    "if(d.led){$('r').value=d.led.r;$('g').value=d.led.g;$('b').value=d.led.b;"
    "$('rv').textContent=d.led.r;$('gv').textContent=d.led.g;$('bv').textContent=d.led.b;"
    "var c='rgb('+d.led.r+','+d.led.g+','+d.led.b+')';"
    "$('lg').style.background='radial-gradient(ellipse at center,'+c+',transparent 70%)';"
    "$('lv').textContent='RGB('+d.led.r+','+d.led.g+','+d.led.b+')'}"
    "if(d.bright!=null){$('br').value=d.bright;$('brv').textContent=d.bright}"
    "if(d.fx){setFxUI(d.fx)}"
    "if(d.tft&&d.tft.rot!=null){tftRot=d.tft.rot}"
    "}"
    "var first=true;"
    "function loadStatus(){get('/cmd?c=STATUS').then(function(t){try{var d=JSON.parse(t);updStatus(d);if(first){syncFromDevice(d);first=false}}catch(e){}})}"
    "loadStatus();setInterval(loadStatus,4000);"
    "</script></body></html>");
  server.send(200, "text/html", html);
}

static void httpCmd() {
  if (!server.hasArg("c")) { server.send(400, "text/plain", "missing c"); return; }
  String resp = handleCommand(server.arg("c"));
  server.send(200, "text/plain", resp);
}

static void httpStatus() {
  server.send(200, "application/json", handleCommand("STATUS"));
}

// ========== BLE 初始化 ==========
static void setupBLE() {
  BLEDevice::init("Kimi-Robot");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  BLEService* svc = bleServer->createService(SERVICE_UUID);

  BLECharacteristic* rx = svc->createCharacteristic(
      CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new RxCallbacks());

  txChar = svc->createCharacteristic(
      CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising* adv = bleServer->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  Serial.println("[BLE] Advertising as 'Kimi-Robot' (UART mode)");
}

// ========== WiFi 初始化（扫描模式）==========
static void setupWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println("[WiFi] Scanning networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("[WiFi] Found %d networks\n", n);

  const WifiCred* chosen = nullptr;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      Serial.printf("  - %s (%d dBm) %s\n",
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "[open]" : "[secure]");
    }
    for (int k = 0; k < wifiCount && !chosen; k++) {
      for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == wifiList[k].ssid) {
          chosen = &wifiList[k];
          Serial.printf("[WiFi] Matched '%s' (%d dBm)\n",
                        chosen->ssid, WiFi.RSSI(i));
          break;
        }
      }
    }
  }

  if (!chosen) {
    Serial.println("[WiFi] No known SSID found in scan, trying list in order...");
    for (int k = 0; k < wifiCount && !chosen; k++) {
      chosen = &wifiList[k];
    }
  }

  if (chosen) {
    Serial.printf("[WiFi] Connecting to '%s'...\n", chosen->ssid);
    WiFi.begin(chosen->ssid, chosen->pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(400);
      Serial.print(".");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s (SSID: %s)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.SSID().c_str());
  } else {
    Serial.println("\n[WiFi] All attempts failed, continuing without WiFi.");
  }
  WiFi.scanDelete();
}

// ========== Arduino 入口 ==========
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=============================");
  Serial.println("  Kimi WiFi + BLE Controller");
  Serial.println("  (精简版: LED + WiFi + BLE)");
  Serial.println("=============================");

  // 打印上次复位原因，方便判断是崩溃(PANIC)、掉电(BROWNOUT) 还是看门狗(WDT)
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

  // 命令队列（供 BLE 任务向 loop 任务投递命令）
  cmdQueue = xQueueCreate(8, sizeof(CmdMsg));

  // LED
  pixel.begin();
  pixel.clear();
  pixel.show();
  setSolidColor(0, 150, 136);   // 启动指示色
  Serial.println("[LED] GPIO48 WS2812 ready");

  // 显示屏
  tftInit();
  tftText("Kimi Robot\n\n启动中...", 2);

  // WiFi
  setupWiFi();

  // HTTP
  server.on("/",        HTTP_GET, httpRoot);
  server.on("/cmd",     HTTP_GET, httpCmd);
  server.on("/status",  HTTP_GET, httpStatus);
  server.begin();
  Serial.println("[HTTP] Server started (port 80)");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("  +-------------------------------------+");
    Serial.println("  |  WiFi: http://" + WiFi.localIP().toString());
    Serial.println("  |  BLE:  nRF Connect -> 'Kimi-Robot'");
    Serial.println("  +-------------------------------------+");
    tftText("WiFi 已连接\n\n" + WiFi.localIP().toString() + "\n\n浏览器打开控制页", 1);
  } else {
    tftText("WiFi 连接失败\n\n请检查热点", 1);
  }

  // BLE
  setupBLE();
}

void loop() {
  server.handleClient();

  // 取出 BLE 投递的命令，在 loop 任务里执行（硬件访问只发生在这里）
  if (cmdQueue) {
    CmdMsg m;
    while (xQueueReceive(cmdQueue, &m, 0) == pdTRUE) {
      handleCommand(String(m.text));
    }
  }

  ledUpdate();
  delay(2);
}
