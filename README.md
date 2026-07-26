# Kimi Robot ESP32-S3

ESP32-S3 机器人控制固件，提供网页和 BLE UART 控制。

## 功能

- GPIO48 WS2812 RGB 灯：颜色、亮度、常亮/呼吸/彩虹/闪烁
- GPIO4 MG90S 舵机：0-180° 角度控制、松劲
- 1.77 英寸 ST7735S 显示屏：中英文文字、填色、旋转、清屏
- WiFi STA + HTTP 控制页面
- BLE UART 命令控制

## 项目结构

```
kimi_robot/
├── kimi_robot.ino          # 入口：setup() + loop()
├── config.h                # 引脚定义、常量、全局 extern 声明
├── led_ctrl.h / .cpp       # WS2812 灯效控制
├── servo_ctrl.h / .cpp     # MG90S 舵机（LEDC 50Hz PWM）
├── display_ctrl.h / .cpp   # ST7735S TFT + U8g2 中文渲染
├── ble_ctrl.h / .cpp       # BLE UART GATT 服务
├── wifi_ctrl.h / .cpp      # WiFi 扫描 + 连接
├── web_server.h / .cpp     # HTTP 路由 + 控制网页
├── command.h / .cpp        # 命令解析器（BLE/HTTP 共用）
├── wifi_secrets.h          # (gitignored) WiFi 凭据
└── wifi_secrets.example.h
```

## 接线

### 显示屏（1.77" ST7735S，软件 SPI）

| ST7735S | ESP32-S3 |
|---|---:|
| SCK | GPIO12 |
| SDA / MOSI | GPIO11 |
| RES | GPIO10 |
| RS / DC | GPIO9 |
| CS | GPIO14 |
| VCC / LED | 3.3V |
| GND | GND |

### 舵机（MG90S）

| MG90S 线色 | ESP32-S3 |
|---|---:|
| 橙（信号） | GPIO4 |
| 红（电源） | 5V / VBUS |
| 棕（地） | GND（共地） |

> 不要用 3.3V 给舵机供电，堵转电流会拉垮电源。

详细说明见 `tools/显示器接线.md`。

## Arduino 依赖

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- Adafruit NeoPixel
- U8g2_for_Adafruit_GFX

## WiFi 配置

复制 `kimi_robot/wifi_secrets.example.h` 为 `kimi_robot/wifi_secrets.h`，填写热点名称和密码。本地凭据文件已加入 `.gitignore`。

## 控制命令

通过 BLE UART 或 HTTP `/cmd?c=<命令>` 发送，不区分大小写：

| 命令 | 说明 |
|---|---|
| `LED,R,G,B` | 设置颜色并常亮 |
| `BRIGHT,0-100` | 亮度百分比 |
| `FX,SOLID\|BREATHE\|RAINBOW\|BLINK` | 灯效切换 |
| `SERVO,0-180` | 舵机角度 |
| `SERVO,OFF` | 舵机松劲（停止脉冲） |
| `FILL,R,G,B` | 屏幕填色 |
| `TEXT,字号,内容` | 显示文字（1=小，2/3=大） |
| `ROT,0-3` | 屏幕旋转 |
| `CLEAR` | 清屏 |
| `STATUS` | 返回状态 JSON |
| `MEM` | 返回内存/芯片信息 |
| `PING` | 返回 PONG |

串口波特率：115200。网页地址由 DHCP 分配，启动后显示在屏幕和串口中。

## 编译

```bash
cd kimi-robot
tools/bin/arduino-cli --config-file tools/arduino-cli.yaml compile \
  --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=huge_app \
  kimi_robot
```
