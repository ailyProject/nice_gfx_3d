#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>
#include "Hardware.h"
#include "KeyframeAnimation.h"

using namespace nice_gfx_3d;
KeyframeAnimationDemo demo;
PerspectiveCamera camera(45,4.0f/3,.1f,50);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
Renderer renderer;
NiceGfxPresenter presenter;
uint32_t lastAt=0;
bool ready=false,paused=false;

void setup() {
  Serial.begin(115200);
  auto &display=exampleDisplay();
  if(!display.begin())return;
  display.fillScreen(0);
  auto *backend=nice_gfx::frameBackend(display);if(!backend)return;
  camera.position.set(0,0,3.6f);camera.lookAt(0,0,0);
  orbitControls.target.set(0,0,0);
  orbitControls.minDistance=1.4f;orbitControls.maxDistance=30;
  orbitControls.setViewport(display.width(),display.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  RendererOptions options;options.maxVertices=24;options.maxObjects=4;
  options.clearColor=Color(0x14202e);
  Status status=renderer.begin(options);
  if(status==Status::Ok)status=presenter.begin(*backend);
  if(status!=Status::Ok){Serial.println(statusString(status));return;}
  Serial.println("Keyframe API lesson. p: pause/resume, r: restart, 1: repeat, 2: ping-pong, 3: once.");
  lastAt=millis();ready=true;
}

void loop() {
  if(!ready){delay(100);return;}
  while(Serial.available()) {
    const char command=char(Serial.read());
    if(command=='h')orbitControls.reset();
    // clipAction returns the already-registered action; this is not a new clip.
    auto &action=demo.mixer.clipAction(demo.clip);
    if(command=='p')paused=!paused;
    if(command=='r'){action.reset().play();demo.mesh.rotation.y=0;paused=false;}
    if(command>='1'&&command<='3') {
      action.setLoop(command=='1'?LoopRepeat:command=='2'?LoopPingPong:LoopOnce).reset().play();
      paused=false;
    }
  }
  const uint32_t now=millis();
  orbitInput.update(orbitControls,now);
  if(uint32_t(now-lastAt)<16){delay(1);return;}
  const float dt=std::min(.1f,float(uint32_t(now-lastAt))*.001f);lastAt=now;
  if(!paused)demo.update(dt);
  Status status=renderer.render(demo.scene,camera);
  auto &display=exampleDisplay();
  if(status==Status::Ok)status=presenter.presentFit(renderer.frame());
  if(status!=Status::Ok&&status!=Status::Busy){Serial.println(statusString(status));ready=false;}
}
