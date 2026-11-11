#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>
#include "Hardware.h"

using namespace nice_gfx_3d;

// This is a material API lesson. For real downloaded character animations,
// start with ModelFromFlash, ModelFromLittleFS or RobotExpressivePlayer.
Scene scene;
BoxGeometry box(1.05f,1.05f,1.05f);
PlaneGeometry panel(1.35f,1.35f);
uint16_t checkerPixels[32*32];
uint8_t cutoutAlpha[32*32];
Texture checker(checkerPixels,32,32);
Texture cutout(checkerPixels,32,32,32,cutoutAlpha);
MeshBasicMaterial cubeMaterial(Color(0xffffff));
MeshBasicMaterial cutoutMaterial(Color(0xffffff));
MeshBasicMaterial backingMaterial(Color(0x9149ca));
Mesh cube(box,cubeMaterial),sign(panel,cutoutMaterial),backing(panel,backingMaterial);
PerspectiveCamera camera(45,4.0f/3,0.1f,50);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
Renderer renderer;
NiceGfxPresenter presenter;
uint32_t lastAt=0;
bool ready=false;

void setup() {
  Serial.begin(115200);
  auto &display=exampleDisplay();
  if(!display.begin()){Serial.println("Display initialization failed");return;}
  display.fillScreen(0);
  auto *backend=nice_gfx::frameBackend(display);
  if(!backend){Serial.println("No frame backend");return;}
  // Pixels are ordinary native RGB565 words. The optional A8 plane has the
  // same pixel stride; no PNG decoder or per-frame texture allocation needed.
  for(size_t y=0;y<32;++y)for(size_t x=0;x<32;++x) {
    const size_t at=y*32+x;
    checkerPixels[at]=((x/4+y/4)&1)?Color(0x22b5df).toRGB565():Color(0xffdc77).toRGB565();
    const int dx=int(x)-16,dy=int(y)-16;
    cutoutAlpha[at]=(dx*dx+dy*dy<14*14 && (x<13||x>18||y<13||y>18))?255:0;
  }
  cubeMaterial.map=&checker;
  cutoutMaterial.map=&cutout;
  cutoutMaterial.alphaTest=0.5f; // Cut holes, but keep opaque depth writes.
  cutoutMaterial.side=Side::Double;
  cube.position.set(-.9f,0,0);
  sign.position.set(.9f,0,.05f);
  backing.position.set(.9f,0,-.1f);
  scene.add(cube);scene.add(sign);scene.add(backing);
  camera.position.set(0,0,4.5f);camera.lookAt(0,0,0);
  orbitControls.target.set(0,0,0);
  orbitControls.minDistance=2.0f;orbitControls.maxDistance=30;
  orbitControls.setViewport(display.width(),display.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  RendererOptions options;
  options.maxVertices=24;options.maxObjects=8;
  options.clearColor=Color(0x14202e);
  Status status=renderer.begin(options);
  if(status==Status::Ok)status=presenter.begin(*backend);
  if(status!=Status::Ok){Serial.println(statusString(status));return;}
  Serial.println("Left: RGB565 texture. Right: A8 alpha-test; purple is visible through holes.");
  lastAt=millis();ready=true;
}

void loop() {
  if(!ready){delay(100);return;}
  const uint32_t now=millis();
  orbitInput.update(orbitControls,now);
  while(Serial.available())if(Serial.read()=='h')orbitControls.reset();
  if(uint32_t(now-lastAt)<16){delay(1);return;}
  const float dt=std::min(.1f,float(uint32_t(now-lastAt))*.001f);lastAt=now;
  cube.rotation.y+=dt*.55f;cube.rotation.x=.25f;
  Status status=renderer.render(scene,camera);
  auto &display=exampleDisplay();
  if(status==Status::Ok)status=presenter.presentFit(renderer.frame());
  if(status!=Status::Ok&&status!=Status::Busy){Serial.println(statusString(status));ready=false;}
}
