#pragma once
#include <Arduino.h>
#include <NICE_GFX.h>

// 仅适配下面两个具体开发板。其他 P4/S31 板须替换 display 声明，
// 使用实际板卡的 nice_gfx 预设/显示配置，不沿用错误的屏幕时序或引脚。
#if defined(CONFIG_IDF_TARGET_ESP32P4)
inline nice_gfx::Jc1060P470CIWYDisplay display;
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
inline nice_gfx::Esp32S31Korvo1Display display;
#else
#error "CameraViews requires ESP32-P4 or ESP32-S31"
#endif

inline bool beginDisplayHardware() {
  if (!display.begin()) return false;
  display.fillScreen(0);
  display.wait();
  return true;
}
