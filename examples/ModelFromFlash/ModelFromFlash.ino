#include "Hardware.h"
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>
#include "AssetInfo.h"
#include "FoxModel.h"
#include <algorithm>
#include <vector>

using namespace nice_gfx_3d;

Scene scene;
Model fox;                       // Outlives mixer and its borrowed actions.
AnimationMixer mixer;
struct RestPose { Vector3 position, scale; Quaternion quaternion; };
std::vector<RestPose> restPose;    // Allocated once in setup; reused every frame.
AnimationAction* actions[3] = {};
AnimationAction* activeAction = nullptr;
PerspectiveCamera camera(45, 4.0f / 3.0f, 0.1f, 10000);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
AmbientLight ambient(0xffff, 0.65f);
DirectionalLight sunlight(0xffff, 0.75f);
Renderer renderer;
NiceGfxPresenter presenter;
Clock animationClock;
bool ready = false, autoCycle = true;
size_t activeClip = 0;
float playbackSpeed = 1;
uint32_t lastFrame = 0, lastSwitch = 0, lastReport = 0, frameCount = 0;

ModelLimits assetLimits() {
  ModelLimits limits;
  limits.bytes = 2u * 1024u * 1024u;
  limits.vertices = 8192; limits.indices = 32768;
  limits.nodes = 128; limits.meshes = 16; limits.primitives = 16;
  limits.textures = 4; limits.texturePixels = 4u * 256u * 256u;
  limits.skins = 4; limits.jointsPerSkin = 64;
  limits.animations = 8; limits.channels = 512; limits.keysPerChannel = 4096;
  limits.hierarchyDepth = 64;
  return limits;
}

void restoreNodePose() {
  for (size_t i = 0; i < restPose.size(); ++i) {
    auto* node = fox.node(i);
    if (!node) continue;
    node->position = restPose[i].position;
    node->scale = restPose[i].scale;
    node->setQuaternion(restPose[i].quaternion);
  }
}

void selectClip(size_t index) {
  if (index >= 3 || !actions[index]) return;
  mixer.stopAllAction();
  activeClip = index;
  activeAction = actions[index];
  activeAction->reset().setLoop(LoopRepeat).setEffectiveTimeScale(playbackSpeed).play();
  lastSwitch = millis();
  Serial.printf("Clip %u: %s, speed %.2fx\n", unsigned(index + 1),
                activeAction->getClip().name, double(playbackSpeed));
}

void setup() {
  Serial.begin(115200);
  Serial.println(kBoardName);
  Status status = fox.load(kFoxModel, kFoxModel_size, assetLimits());
  if (status != Status::Ok) {
    Serial.printf("Fox load: %s / %s\n", statusString(status), fox.error());
    return;
  }
  Serial.printf("Flash NG3D: %u bytes; %u clips\n", unsigned(kFoxModel_size), unsigned(fox.animationCount()));
  restPose.resize(fox.nodeCount());
  for (size_t i = 0; i < restPose.size(); ++i) {
    const auto* node = fox.node(i);
    restPose[i] = {node->position, node->scale, node->quaternion};
  }
  scene.add(fox.scene());
  scene.add(ambient); scene.add(sunlight);
  sunlight.direction.set(-0.7f, -1.0f, -0.5f).normalize();
  const Vector3 center(demo_asset::centerX, demo_asset::centerY, demo_asset::centerZ);
  const float radius = std::max(0.01f, demo_asset::radius);
  camera.position = center + Vector3(2.4f, 0.8f, 2.2f) * radius;
  camera.near = radius * 0.02f; camera.far = radius * 30;
  camera.updateProjectionMatrix(); camera.lookAt(center);
  for (size_t i = 0; i < std::min(size_t(3), fox.animationCount()); ++i)
    actions[i] = &mixer.clipAction(*fox.animation(i));
  if (!actions[0]) { Serial.println("Fox has no playable clip"); return; }
  selectClip(0);

  if (!display.begin()) { Serial.println("Display initialization failed"); return; }
  display.fillScreen(0);
  auto* backend = nice_gfx::frameBackend(display);
  if (!backend) { Serial.println("Display has no frame backend"); return; }
  RendererOptions options;
  options.width = 320; options.height = 240; options.clearColor = 0x18e4;
  options.maxVertices = demo_asset::maxVertices;
  options.maxObjects = demo_asset::maxObjects;
  options.colorMemory = MemoryPreference::InternalFirst;
  options.depthMemory = MemoryPreference::ExternalFirst;
  options.scratchMemory = MemoryPreference::ExternalFirst;
  status = renderer.begin(options);
  if (status == Status::Ok) status = presenter.begin(*backend, true);
  if (status != Status::Ok) { Serial.println(statusString(status)); return; }
  orbitControls.target=center;
  orbitControls.minDistance=radius*1.4f;orbitControls.maxDistance=radius*12;
  orbitControls.setViewport(display.width(),display.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(display.width(),display.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  Serial.println("1 Survey | 2 Walk | 3 Run | a auto-cycle | p pause | +/- speed");
  animationClock.start();
  lastFrame = lastReport = millis();
  ready = true;
}

void loop() {
  if (!ready) { delay(100); return; }
  while (Serial.available()) {
    const char command = char(Serial.read());
    if(command=='h')orbitControls.reset();
    else if (command >= '1' && command <= '3') { autoCycle = false; selectClip(size_t(command - '1')); }
    else if (command == 'a') { autoCycle = true; lastSwitch = millis(); }
    else if (command == 'p' && activeAction) activeAction->paused = !activeAction->paused;
    else if (command == '+' || command == '-') {
      playbackSpeed = std::max(0.25f, std::min(2.0f, playbackSpeed + (command == '+' ? 0.25f : -0.25f)));
      if (activeAction) activeAction->setEffectiveTimeScale(playbackSpeed);
      Serial.printf("Speed %.2fx\n", double(playbackSpeed));
    }
  }
  const uint32_t now = millis();
  orbitInput.update(orbitControls,now);
  if (autoCycle && uint32_t(now - lastSwitch) >= 8000) selectClip((activeClip + 1) % 3);
  if (uint32_t(now - lastFrame) < 16) { delay(1); return; }
  lastFrame = now;
  // A newly selected clip may not animate every channel written by the old
  // clip. Restore only asset node TRS; leave the outer model group/camera alone.
  restoreNodePose();
  mixer.update(std::min(0.10f, animationClock.getDelta()));
  Status status = renderer.render(scene, camera);
  if (status == Status::Ok)
    status = presenter.presentFit(renderer.frame());
  if (status == Status::Busy) { delay(1); return; }
  if (status != Status::Ok) { Serial.println(statusString(status)); ready = false; delay(1); return; }
  ++frameCount;
  if (uint32_t(now - lastReport) >= 2000) {
    Serial.printf("%.1f fps, render %llu us, present %llu us, %s\n",
      double(frameCount) * 1000.0 / double(uint32_t(now - lastReport)),
      (unsigned long long)renderer.stats().renderMicros,
      (unsigned long long)presenter.stats().presentMicros,
      presenter.stats().hardwareScaled ? "PPA" : "CPU");
    lastReport = now; frameCount = 0;
  }
  delay(1); // Also yield when a complex frame already exceeded the 16ms pacing.
}
