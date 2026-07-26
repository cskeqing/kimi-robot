# Kimi Robot ESP32-S3

ESP32-S3 机器人控制固件，提供网页和 BLE UART 控制。

## 功能

- GPIO48 WS2812 RGB 灯：颜色、亮度、常亮/呼吸/彩虹/闪烁
- WiFi STA + HTTP 控制页面
- BLE UART 命令控制
- 1.77 英寸 ST7735S 显示屏：中英文文字、填色、旋转、清屏

## 显示屏接线

| ST7735S | ESP32-S3 |
|---|---:|
| SCK | GPIO12 |
| SDA / MOSI | GPIO11 |
| RES | GPIO10 |
| RS / DC | GPIO9 |
| CS | GPIO14 |
| VCC / LED | 3.3V |
| GND | GND |

详细说明见 `tools/显示器接线.md`。

## Arduino 依赖

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- Adafruit NeoPixel
- U8g2_for_Adafruit_GFX

## WiFi 配置

复制 `kimi_robot/wifi_secrets.example.h` 为 `kimi_robot/wifi_secrets.h`，填写热点名称和密码。本地凭据文件已加入 `.gitignore`。

## 控制命令

- `LED,R,G,B`
- `BRIGHT,0-100`
- `FX,SOLID|BREATHE|RAINBOW|BLINK`
- `FILL,R,G,B`
- `TEXT,字号,内容`
- `ROT,0-3`
- `CLEAR`
- `STATUS` / `MEM` / `PING`

串口波特率：115200。网页地址由 DHCP 分配，启动后会显示在屏幕和串口中。
