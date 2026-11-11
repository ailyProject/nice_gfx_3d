#include <NICE_GFX.h>
#include <Display/FrameAccess.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <unistd.h>

extern "C" void app_main() {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
  static nice_gfx::Esp32S31Korvo1Display display;
#else
  static nice_gfx::Jc1060P470CIWYDisplay display;
#endif
  if(!display.begin())return;
  display.fillScreen(0);
  auto *backend=nice_gfx::frameBackend(display);if(!backend)return;
  using namespace nice_gfx_3d;
  Scene scene;BoxGeometry geometry;MeshLambertMaterial material(0x3dff);
  Mesh cube(geometry,material);AmbientLight ambient(0xffff,.35f);DirectionalLight sun;
  sun.direction.set(1,-1,-1).normalize();scene.add(cube);scene.add(ambient);scene.add(sun);
  PerspectiveCamera camera(45,4.0f/3,.1f,100);camera.position.set(0,0,3);camera.lookAt(0,0,0);
  OrbitControls controls(camera);controls.target.set(0,0,0);
  controls.minDistance=.4f;controls.maxDistance=12;
  controls.setViewport(display.width(),display.height());controls.saveState();
  NiceGfxOrbitInput orbitInput;
  if(!orbitInput.begin(display.width(),display.height()))ESP_LOGW("nice_gfx_3d","Touch unavailable, status=%u; rendering continues",unsigned(orbitInput.status()));
  Renderer renderer;RendererOptions options;options.maxVertices=64;options.maxObjects=8;
  NiceGfxPresenter presenter;
  Status result=renderer.begin(options);if(result==Status::Ok)result=presenter.begin(*backend);
  if(result!=Status::Ok){ESP_LOGE("nice_gfx_3d","%s",statusString(result));return;}
  const int consoleFlags=fcntl(STDIN_FILENO,F_GETFL,0);
  const bool console=consoleFlags>=0&&fcntl(STDIN_FILENO,F_SETFL,consoleFlags|O_NONBLOCK)>=0;
  ESP_LOGI("nice_gfx_3d","Drag to orbit around the cube center; pinch to zoom; h resets the camera");
  int64_t previous=esp_timer_get_time();TickType_t tick=xTaskGetTickCount();
  while(true) {
    orbitInput.update(controls,uint32_t(esp_timer_get_time()/1000));
    if(console){char commands[16];const ssize_t count=read(STDIN_FILENO,commands,sizeof(commands));for(ssize_t i=0;i<count;++i)if(commands[i]=='h'||commands[i]=='H')controls.reset();}
    const int64_t now=esp_timer_get_time();const float dt=float(now-previous)*1e-6f;previous=now;
    cube.rotation.x+=dt*.4f;cube.rotation.y+=dt*.8f;
    result=renderer.render(scene,camera);if(result==Status::Ok)result=presenter.presentFit(renderer.frame());
    if(result!=Status::Ok&&result!=Status::Busy){ESP_LOGE("nice_gfx_3d","%s",statusString(result));return;}
    vTaskDelayUntil(&tick,pdMS_TO_TICKS(16));
  }
}
