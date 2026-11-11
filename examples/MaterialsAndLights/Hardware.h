#pragma once
#include <Arduino.h>
#include <NICE_GFX.h>

// 默认声明仅对应这两块具体板。其他 P4/S31 屏幕请替换为真实的
// nice_gfx 显示预设或配置；无需改动示例中的场景、材质和相机代码。
#if defined(CONFIG_IDF_TARGET_ESP32P4)
inline nice_gfx::Jc1060P470CIWYDisplay display;
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
inline nice_gfx::Esp32S31Korvo1Display display;
#else
#error "MaterialsAndLights requires ESP32-P4 or ESP32-S31"
#endif

inline bool beginDisplayHardware() {
  if (!display.begin()) return false;
  display.fillScreen(0);
  display.wait();
  return true;
}
