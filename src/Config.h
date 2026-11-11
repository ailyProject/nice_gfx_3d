#pragma once

// Desktop authoring tools may include the portable core. Device deployment is
// deliberately restricted to these two targets; no silent slow fallback MCU.
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#include "esp_idf_version.h"
#if !defined(CONFIG_IDF_TARGET_ESP32P4) && !defined(CONFIG_IDF_TARGET_ESP32S31)
#error "NICE_GFX_3D currently supports ESP32-P4 and ESP32-S31 only"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S31) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 1, 0)
#error "NICE_GFX_3D on ESP32-S31 requires ESP-IDF 6.1 or newer"
#endif
#define NICE_GFX_3D_ESP 1
#else
#define NICE_GFX_3D_ESP 0
#endif

#define NICE_GFX_3D_VERSION_MAJOR 0
#define NICE_GFX_3D_VERSION_MINOR 1
#define NICE_GFX_3D_VERSION_PATCH 0
