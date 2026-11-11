#pragma once
#include <NICE_GFX.h>
#include <Display/FrameAccess.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>

// This sketch carries its own board adapter; no include from another example.
// Select the actual MCU in Arduino and enable its PSRAM setting.
namespace robot_player_hardware {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
using Display = nice_gfx::Jc1060P470CIWYDisplay;
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
using Display = nice_gfx::Esp32S31Korvo1Display;
#else
#error "RobotExpressivePlayer requires ESP32-P4 or ESP32-S31."
#endif

inline Display &display() { static Display instance; return instance; }

inline bool begin(nice_gfx_3d::Renderer &renderer,
                  nice_gfx_3d::NiceGfxPresenter &presenter,
                  uint32_t maxVertices, uint32_t maxObjects) {
  auto &screen=display();
  if(!screen.begin()){Serial.println("Display initialization failed");return false;}
  screen.fillScreen(0);
  auto *backend=nice_gfx::frameBackend(screen);
  if(!backend){Serial.println("Display has no frame backend");return false;}
  nice_gfx_3d::RendererOptions options;
  options.width=320;options.height=240;options.clearColor=0x1082;
  options.maxVertices=maxVertices;options.maxObjects=maxObjects;
  auto result=renderer.begin(options);
  if(result==nice_gfx_3d::Status::Ok)result=presenter.begin(*backend);
  if(result!=nice_gfx_3d::Status::Ok){Serial.println(nice_gfx_3d::statusString(result));return false;}
  return true;
}

} // namespace robot_player_hardware
