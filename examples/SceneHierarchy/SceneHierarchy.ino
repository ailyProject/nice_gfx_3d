#include "Hardware.h"
#include <Display/FrameAccess.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>

using namespace nice_gfx_3d;

// 非拥有关系：父对象先声明，子对象后声明，保证退出时先销毁子对象。
Scene scene;
Group assembly;
Group orbit;
BoxGeometry sharedBox(1, 1, 1); // 三个 Mesh 借用同一个几何对象。
MeshLambertMaterial orange(Color(0xffa54a));
MeshLambertMaterial cyan(Color(0x49c9ff));
MeshLambertMaterial green(Color(0x77df91));
Mesh hub(sharedBox, orange);
Mesh satellite(sharedBox, cyan);
Mesh marker(sharedBox, green);
AmbientLight ambient(0xffff, 0.35f);
DirectionalLight sun(0xffff, 0.8f);
PerspectiveCamera camera(45, 4.0f / 3.0f, 0.1f, 30);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
Renderer renderer;
NiceGfxPresenter presenter;
bool ready = false;
uint32_t previousFrame = 0;
float parentAngle = 0, orbitAngle = 0;

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
  if (!backend) {
    reportFailure("frameBackend", Status::NotInitialized);
    return false;
  }
  RendererOptions options;
  options.width = 320;
  options.height = 240;
  options.maxVertices = 64; // 每个 Mesh 的顶点上限；BoxGeometry 有 24 个顶点。
  options.maxObjects = 16; // 包括 Scene、Group、Mesh 和 Light。
  options.clearColor = Color(0x132134);
  Status status = renderer.begin(options);
  if (status == Status::Ok) status = presenter.begin(*backend);
  if (status != Status::Ok) {
    reportFailure("beginOutput", status);
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  assembly.name = "assembly";
  orbit.name = "orbit";
  hub.name = "hub";
  satellite.name = "satellite";
  marker.name = "marker";

  hub.scale.set(0.9f, 0.9f, 0.9f);
  hub.rotation.z = 0.2f;
  satellite.position.set(1.35f, 0, 0); // 相对 orbit 的局部位置。
  satellite.scale.set(0.34f, 0.34f, 0.34f);
  marker.position.set(-1.2f, 0.15f, 0);
  marker.scale.set(0.22f, 0.65f, 0.22f);

  // scene → assembly → hub / marker / orbit → satellite。
  // add() 只建立借用关系，不复制网格，也不接管子对象的内存。
  if (!scene.add(assembly) || !assembly.add(hub) || !assembly.add(marker) ||
      !assembly.add(orbit) || !orbit.add(satellite) ||
      !scene.add(ambient) || !scene.add(sun)) {
    reportFailure("scene.add", Status::InvalidArgument);
    return;
  }
  sun.direction.set(1, -1, -1).normalize();
  camera.position.set(0, 1.0f, 4.8f);
  camera.lookAt(0, 0, 0);
  if (!beginOutput()) return;
  orbitControls.target.set(0,0,0);
  orbitControls.minDistance=2.1f;orbitControls.maxDistance=20;
  orbitControls.setViewport(display.width(),display.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  previousFrame = millis();
  ready = true;
  Serial.println("Orange hub, cyan orbiting child, green marker: one shared BoxGeometry.");
}

void loop() {
  if (!ready) { delay(100); return; }
  const uint32_t now = millis();
  orbitInput.update(orbitControls,now);
  while(Serial.available())if(Serial.read()=='h')orbitControls.reset();
  const uint32_t elapsed = uint32_t(now - previousFrame);
  if (elapsed < 16) { delay(1); return; } // 调度目标，不是实测帧率承诺。
  previousFrame = now;
  const float dt = std::min(0.1f, float(elapsed) * 0.001f);

  // 父 Group 改变整个装配；子 Group 再增加自己的局部旋转。
  parentAngle += dt * 0.35f;
  orbitAngle += dt * 0.95f;
  if (parentAngle >= 2 * Pi) parentAngle -= 2 * Pi;
  if (orbitAngle >= 2 * Pi) orbitAngle -= 2 * Pi;
  assembly.rotation.y = parentAngle;
  orbit.rotation.z = orbitAngle;
  satellite.rotation.x = orbitAngle;

  // Renderer 更新 matrixWorld；不需要在 loop 中重新 add、创建几何或分配画布。
  Status status = renderer.render(scene, camera);
  if (status == Status::Ok) status = presenter.presentFit(renderer.frame());
  if (status == Status::Busy) { delay(1); return; } // 显示资源暂忙，下帧再试。
  if (status != Status::Ok) reportFailure("render/present", status);
}
