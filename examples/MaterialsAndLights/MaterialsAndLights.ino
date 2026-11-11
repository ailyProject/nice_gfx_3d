#include "Hardware.h"
#include <Display/FrameAccess.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>

using namespace nice_gfx_3d;

Scene scene;
BoxGeometry geometry(0.8f, 0.8f, 0.8f);
// Color 接收 0xRRGGBB；直接传 uint16_t 时则按 RGB565 解释。
MeshBasicMaterial basic(Color(0x50cfff));
MeshLambertMaterial lambert(Color(0x50cfff)); // 与 basic 同色，便于对照。
MeshLambertMaterial warmLambert(Color(0xffa16a));
Mesh basicCube(geometry, basic);
Mesh litCube(geometry, lambert);
Mesh warmCube(geometry, warmLambert);
AmbientLight ambient(0xffff, 0.25f);
DirectionalLight sun(0xffff, 0.9f);
PerspectiveCamera camera(45, 4.0f / 3.0f, 0.1f, 30);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
Renderer renderer;
NiceGfxPresenter presenter;
bool ready = false, ambientEnabled = true;
uint32_t previousFrame = 0;
float lightAngle = 0;

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
  options.width = 320;
  options.height = 240;
  options.maxVertices = 64;
  options.maxObjects = 12;
  options.clearColor = Color(0x142033);
  Status status = renderer.begin(options);
  if (status == Status::Ok) status = presenter.begin(*backend);
  if (status != Status::Ok) { reportFailure("beginOutput", status); return false; }
  return true;
}

void setup() {
  Serial.begin(115200);
  basicCube.position.set(-1.15f, 0, 0);
  litCube.position.set(0, 0, 0);
  warmCube.position.set(1.15f, 0, 0);
  basicCube.rotation.set(0.35f, 0.55f, 0);
  litCube.rotation = basicCube.rotation;
  warmCube.rotation = basicCube.rotation;

  // direction 是光线传播方向；对象旋转会把这个局部方向变换到世界空间。
  // DirectionalLight 的位置不会改变平行光照，本例只旋转它。
  sun.direction.set(0.4f, -0.65f, -1).normalize();
  if (!scene.add(basicCube) || !scene.add(litCube) || !scene.add(warmCube) ||
      !scene.add(ambient) || !scene.add(sun)) {
    reportFailure("scene.add", Status::InvalidArgument);
    return;
  }
  camera.position.set(0, 1.0f, 5.1f);
  camera.lookAt(0, 0, 0);
  if (!beginOutput()) return;
  orbitControls.target.set(0,0,0);
  orbitControls.minDistance=2.1f;orbitControls.maxDistance=20;
  orbitControls.setViewport(display.width(),display.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  previousFrame = millis();
  ready = true;
  Serial.println("Left: Basic. Center: same-color Lambert. Right: warm Lambert.");
  Serial.println("Directional light rotates. Send a to toggle ambient light (115200 baud).");
}

void loop() {
  if (!ready) { delay(100); return; }
  while (Serial.available() > 0) {
    const int key = Serial.read();
    if(key=='h'||key=='H')orbitControls.reset();
    if (key == 'a' || key == 'A') {
      ambientEnabled = !ambientEnabled;
      ambient.intensity = ambientEnabled ? 0.25f : 0.0f;
      Serial.println(ambientEnabled ? "Ambient on" : "Ambient off");
    }
  }
  const uint32_t now = millis(), elapsed = uint32_t(now - previousFrame);
  orbitInput.update(orbitControls,now);
  if (elapsed < 16) { delay(1); return; }
  previousFrame = now;
  const float dt = std::min(0.1f, float(elapsed) * 0.001f);
  lightAngle += dt * 0.8f;
  if (lightAngle >= 2 * Pi) lightAngle -= 2 * Pi;
  sun.rotation.y = lightAngle;

  // Basic 不读取灯光；Lambert 结合顶点法线、环境光和方向光计算颜色。
  // 全部对象与资源已创建，loop 中不 new、不 begin，也不更换几何。
  Status status = renderer.render(scene, camera);
  if (status == Status::Ok) status = presenter.presentFit(renderer.frame());
  if (status == Status::Busy) { delay(1); return; }
  if (status != Status::Ok) reportFailure("render/present", status);
}
