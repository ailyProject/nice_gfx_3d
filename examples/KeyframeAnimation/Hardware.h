#pragma once
#include <NICE_GFX.h>
#include <Display/FrameAccess.h>

inline auto &exampleDisplay() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  static nice_gfx::Jc1060P470CIWYDisplay display;
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
  static nice_gfx::Esp32S31Korvo1Display display;
#else
#error "This example supports ESP32-P4 and ESP32-S31 only"
#endif
  return display;
}
