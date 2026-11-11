#include "Hardware.h"
#include <Display/FrameAccess.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>

using namespace nice_gfx_3d;
constexpr uint16_t RenderWidth = 320, RenderHeight = 240;
constexpr float RenderAspect = float(RenderWidth) / float(RenderHeight);

Scene scene;
BoxGeometry geometry(0.65f, 0.65f, 0.65f);
MeshLambertMaterial nearMaterial(Color(0xffac59));
MeshLambertMaterial middleMaterial(Color(0x62d6ff));
MeshLambertMaterial farMaterial(Color(0x9ce783));
Mesh nearCube(geometry, nearMaterial);
Mesh middleCube(geometry, middleMaterial);
Mesh farCube(geometry, farMaterial);
AmbientLight ambient(0xffff, 0.4f);
DirectionalLight sun(0xffff, 0.8f);
PerspectiveCamera perspective(40, RenderAspect, 0.1f, 30);
OrthographicCamera orthographic;
OrbitControls perspectiveControls(perspective), orthographicControls(orthographic);
OrbitControls* activeControls=&perspectiveControls;
NiceGfxOrbitInput orbitInput;
Camera* activeCamera = &perspective; // 切换引用，共用一个 Renderer 和同一场景。
Renderer renderer;
NiceGfxPresenter presenter;
bool ready = false;
uint32_t previousFrame = 0;
float angle = 0;

void reportFailure(const char* stage, Status status) {
  Serial.print(stage);
  Serial.print(": ");
  Serial.println(statusString(status));
  ready = false;
}

bool beginOutput() {
  if (!beginDisplayHardware()) {
    Serial.println("Display initialization failed; check Hardware.h and board settings.");
    return false;
  }
  auto* backend = nice_gfx::frameBackend(display);
  if (!backend) { reportFailure("frameBackend", Status::NotInitialized); return false; }
  RendererOptions options;
  options.width = RenderWidth;
  options.height = RenderHeight;
  options.maxVertices = 64;
  options.maxObjects = 12;
  options.clearColor = Color(0x142033);
  Status status = renderer.begin(options);
  if (status == Status::Ok) status = presenter.begin(*backend);
  if (status != Status::Ok) { reportFailure("beginOutput", status); return false; }
  return true;
}

void configureCameras() {
  // 两个相机使用相同位置、朝向和裁剪距离，只改变投影方式。
  perspective.position.set(0, 1.2f, 5);
  perspective.lookAt(0, 0, 0);
  perspective.aspect = RenderAspect;
  perspective.updateProjectionMatrix();

  const float viewHeight = 3.6f; // 正交视野的世界单位高度，与距离无关。
  orthographic.left = -viewHeight * RenderAspect * 0.5f;
  orthographic.right = viewHeight * RenderAspect * 0.5f;
  orthographic.top = viewHeight * 0.5f;
  orthographic.bottom = -viewHeight * 0.5f;
  orthographic.near = 0.1f;
  orthographic.far = 30;
  orthographic.position = perspective.position;
  orthographic.lookAt(0, 0, 0);
  orthographic.updateProjectionMatrix(); // 改投影参数后必须更新。
}

void setup() {
  Serial.begin(115200);
  nearCube.position.set(-1.05f, 0, 0.8f);
  middleCube.position.set(0, 0, 0);
  farCube.position.set(1.05f, 0, -0.8f);
  sun.direction.set(1, -1, -1).normalize();
  if (!scene.add(nearCube) || !scene.add(middleCube) || !scene.add(farCube) ||
      !scene.add(ambient) || !scene.add(sun)) {
    reportFailure("scene.add", Status::InvalidArgument);
    return;
  }
  configureCameras();
  if (!beginOutput()) return;
  for(auto* controls:{&perspectiveControls,&orthographicControls}){
    controls->target.set(0,0,0);controls->minDistance=2.4f;controls->maxDistance=20;
    controls->setViewport(display.width(),display.height());controls->saveState();
  }
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  previousFrame = millis();
  ready = true;
  Serial.println("CameraViews: p=perspective, o=orthographic (115200 baud).");
  Serial.println("Same-size cubes: left orange is near; right green is far.");
}

void loop() {
  if (!ready) { delay(100); return; }
  while (Serial.available() > 0) {
    const int key = Serial.read();
    if (key == 'p' || key == 'P') {
      activeControls->cancel();activeControls=&perspectiveControls;activeControls->cancel();
      activeCamera = &perspective;
      Serial.println("Perspective: apparent size changes with distance.");
    } else if (key == 'o' || key == 'O') {
      activeControls->cancel();activeControls=&orthographicControls;activeControls->cancel();
      activeCamera = &orthographic;
      Serial.println("Orthographic: equal world sizes have equal projected sizes.");
    } else if(key=='h'||key=='H')activeControls->reset();
  }
  const uint32_t now = millis(), elapsed = uint32_t(now - previousFrame);
  orbitInput.update(*activeControls,now);
  if (elapsed < 16) { delay(1); return; }
  previousFrame = now;
  const float dt = std::min(0.1f, float(elapsed) * 0.001f);
  angle += dt * 0.45f;
  if (angle >= 2 * Pi) angle -= 2 * Pi;
  // 三个相同几何使用相同局部旋转，方便比较投影差异。
  nearCube.rotation.set(0.25f, angle, 0);
  middleCube.rotation = nearCube.rotation;
  farCube.rotation = nearCube.rotation;

  // 切换相机不会 begin()/setSize()，缓冲区和投影宽高比始终为 4:3。
  Status status = renderer.render(scene, *activeCamera);
  if (status == Status::Ok) status = presenter.presentFit(renderer.frame());
  if (status == Status::Busy) { delay(1); return; }
  if (status != Status::Ok) reportFailure("render/present", status);
}
