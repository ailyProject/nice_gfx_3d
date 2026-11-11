#pragma once
#include <Arduino.h>
#include <sdkconfig.h>
#include <NICE_GFX.h>
#include <Display/FrameAccess.h>

// The preset owns the real panel timing/pins. Change this declaration when
// using another board; do not reuse these pins for an unrelated panel.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
inline nice_gfx::Jc1060P470CIWYDisplay display;
inline constexpr const char* kBoardName = "ESP32-P4 / JC1060P470C_I_W_Y";
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
inline nice_gfx::Esp32S31Korvo1Display display;
inline constexpr const char* kBoardName = "ESP32-S31 / Korvo-1";
#else
#error "This example supports ESP32-P4 and ESP32-S31 only"
#endif
