#pragma once

#include <Arduino.h>

// 命令格式（不区分大小写）：
//   LED,R,G,B        设置颜色并常亮，例 LED,255,0,0
//   BRIGHT,N         亮度 0-100，例 BRIGHT,80
//   FX,NAME          灯效 SOLID|BREATHE|RAINBOW|BLINK
//   SERVO,N          舵机角度 0-180，例 SERVO,90
//   SERVO,OFF        舵机松劲（停止 PWM 脉冲）
//   FILL,R,G,B       屏幕填色
//   CLEAR            清屏（黑）
//   ROT,N            旋转 0-3
//   TEXT,<size>,<msg> 显示文字（size 1=小 2/3=大）
//   STATUS           返回状态 JSON
//   MEM              返回内存/芯片信息 JSON
//   PING             返回 PONG
String handleCommand(const String& cmd);
