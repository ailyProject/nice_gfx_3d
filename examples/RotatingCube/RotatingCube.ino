#include <NICE_GFX.h>
#include <Display/FrameAccess.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>

// Select the actual board in Arduino. For another P4 panel replace this one
// display declaration with its NICE_GFX preset; the 3D code stays the same.
#if defined(CONFIG_IDF_TARGET_ESP32S31)
nice_gfx::Esp32S31Korvo1Display display;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
nice_gfx::Jc1060P470CIWYDisplay display;
#endif

using namespace nice_gfx_3d;
Scene scene;
PerspectiveCamera camera(45,4.0f/3.0f,0.1f,100);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
BoxGeometry geometry(1,1,1);
MeshLambertMaterial material(0x35bf);
Mesh cube(geometry,material);
AmbientLight ambient(0xffff,0.35f);
DirectionalLight light(0xffff,0.8f);
Renderer renderer;
NiceGfxPresenter presenter;
bool ready=false;
uint32_t previous=0;

void setup() {
  Serial.begin(115200);
  if(!display.begin())return;
  display.fillScreen(0);
  auto *backend=nice_gfx::frameBackend(display);
  if(!backend)return;
  RendererOptions options;
  options.width=320;options.height=240;options.clearColor=0x1082;
  options.maxVertices=64;options.maxObjects=8;
  Status status=renderer.begin(options);
  if(status==Status::Ok)status=presenter.begin(*backend);
  if(status!=Status::Ok){Serial.println(statusString(status));return;}
  camera.position.set(0,0,3);
  camera.lookAt(0,0,0);
  orbitControls.target.set(0,0,0);
  orbitControls.minDistance=1.2f;orbitControls.maxDistance=25;
  orbitControls.setViewport(display.width(),display.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  light.direction.set(1,-1,-1).normalize();
  scene.add(cube);scene.add(ambient);scene.add(light);
  previous=millis();ready=true;
}

void loop() {
  if(!ready){delay(100);return;}
  const uint32_t now=millis();
  orbitInput.update(orbitControls,now);
  while(Serial.available())if(Serial.read()=='h')orbitControls.reset();
  if(uint32_t(now-previous)<16){delay(1);return;}
  const float seconds=float(uint32_t(now-previous))*0.001f;previous=now;
  cube.rotation.x+=seconds*0.4f;cube.rotation.y+=seconds*0.8f;
  Status status=renderer.render(scene,camera);
  if(status==Status::Ok)status=presenter.presentFit(renderer.frame());
  if(status!=Status::Ok){Serial.println(statusString(status));ready=false;}
}
