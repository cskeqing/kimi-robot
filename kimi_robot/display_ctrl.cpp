#include "config.h"
#include "display_ctrl.h"

// 软件 SPI：Adafruit 库硬件 SPI 初始化写死 32MHz，超过 ST7735S 规格上限(~15MHz)，
// 杜邦线上初始化命令收不到 → 白屏。软 SPI 慢一些但对线材宽容，文字显示足够。
Adafruit_ST7735    tft(TFT_CS, TFT_RS, TFT_SDA, TFT_SCK, TFT_RES);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

static const int16_t TFT_W = 128;
static const int16_t TFT_H = 160;
static uint8_t scrR = 0, scrG = 0, scrB = 0;
static uint8_t scrRot = 0;

static void tftDrawWrapped(const String& msg, int lineH) {
  int screenW = tft.width();
  int x = 2, y = lineH;
  int i = 0, n = msg.length();
  while (i < n) {
    char c = msg[i];
    if (c == '\n') { y += lineH; x = 2; i++; continue; }
    uint8_t u = (uint8_t)c;
    int len = 1;
    if      (u >= 0xF0) len = 4;
    else if (u >= 0xE0) len = 3;
    else if (u >= 0xC0) len = 2;
    if (i + len > n) len = 1;
    String ch = msg.substring(i, i + len);
    int w = u8g2Fonts.getUTF8Width(ch.c_str());
    if (x + w > screenW) { y += lineH; x = 2; }
    if (y > tft.height() + lineH) break;
    u8g2Fonts.setCursor(x, y);
    u8g2Fonts.print(ch);
    x += w;
    i += len;
  }
}

void tftInit() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(scrRot);
  tft.fillScreen(ST77XX_BLACK);
  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  Serial.println("[TFT] ST7735S ready (128x160, SW SPI, U8g2 中文)");
}

void tftFill(uint8_t r, uint8_t g, uint8_t b) {
  scrR = r; scrG = g; scrB = b;
  tft.fillScreen(tft.color565(r, g, b));
  Serial.printf("[TFT] fill #%02X%02X%02X\n", r, g, b);
}

void tftClear() {
  tftFill(0, 0, 0);
}

void tftText(const String& msg, uint8_t size) {
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

void tftSetRotation(uint8_t rot) {
  scrRot = rot & 3;
  tft.setRotation(scrRot);
  tft.fillScreen(tft.color565(scrR, scrG, scrB));
}

uint8_t tftGetR() { return scrR; }
uint8_t tftGetG() { return scrG; }
uint8_t tftGetB() { return scrB; }
uint8_t tftGetRotation() { return scrRot; }
