#pragma once
#include <Arduino.h>
#include <NICE_GFX.h>

// 这两个预设只对应这里写出的实际开发板，不代表所有 P4/S31 屏幕。
// 换板时仅替换 display 声明，使用该板真实的 nice_gfx 显示预设/配置。
// 不要把本例的预设名称当作通用引脚配置。
#if defined(CONFIG_IDF_TARGET_ESP32P4)
inline nice_gfx::Jc1060P470CIWYDisplay display;
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
inline nice_gfx::Esp32S31Korvo1Display display;
#else
#error "SceneHierarchy requires ESP32-P4 or ESP32-S31"
#endif

inline bool beginDisplayHardware() {
  if (!display.begin()) return false;
  // fit() 保留输出矩形外的像素，所以初始化时清一次黑边。
  display.fillScreen(0);
  display.wait();
  return true;
}
