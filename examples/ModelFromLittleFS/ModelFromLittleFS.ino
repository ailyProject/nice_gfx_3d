#include "Hardware.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>
#include "AssetInfo.h"
#include <algorithm>

using namespace nice_gfx_3d;

Scene scene;
Model character;                 // Outlives mixer and its clip references.
AnimationMixer mixer;
AnimationAction* action = nullptr;
PerspectiveCamera camera(45, 4.0f / 3.0f, 0.1f, 100);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
AmbientLight ambient(0xffff, 0.65f);
DirectionalLight sunlight(0xffff, 0.75f);
Renderer renderer;
NiceGfxPresenter presenter;
Clock animationClock;
bool ready = false;
float playbackSpeed = 1;
uint32_t lastFrame = 0, lastReport = 0, frameCount = 0;
constexpr size_t kMaximumModelBytes = 2u * 1024u * 1024u;

Status loadFromLittleFS() {
  // false avoids formatting existing data if mounting fails.
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed; upload data with a matching filesystem partition");
    return Status::IoError;
  }
  File file = LittleFS.open("/CesiumMan.ng3d", "r");
  if (!file || file.isDirectory()) {
    Serial.println("/CesiumMan.ng3d is missing; upload this sketch's data folder");
    file.close(); LittleFS.end(); return Status::IoError;
  }
  const size_t length = file.size();
  if (!length || length > kMaximumModelBytes) {
    file.close(); LittleFS.end(); return Status::CapacityExceeded;
  }
  auto* bytes = static_cast<uint8_t*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!bytes) bytes = static_cast<uint8_t*>(heap_caps_malloc(length, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!bytes) { file.close(); LittleFS.end(); return Status::NoMemory; }
  size_t offset = 0;
  while (offset < length) {
    const size_t chunk = std::min(size_t(16384), length - offset);
    const size_t received = file.read(bytes + offset, chunk);
    if (!received) break;
    offset += received;
    delay(0);
  }
  file.close(); LittleFS.end();
  if (offset != length) { heap_caps_free(bytes); return Status::IoError; }
  ModelLimits limits;
  limits.bytes = kMaximumModelBytes;
  limits.vertices = 8192; limits.indices = 32768;
  limits.nodes = 128; limits.meshes = 16; limits.primitives = 16;
  limits.textures = 4; limits.texturePixels = 4u * 256u * 256u;
  limits.skins = 4; limits.jointsPerSkin = 64;
  limits.animations = 8; limits.channels = 512; limits.keysPerChannel = 4096;
  limits.hierarchyDepth = 64;
  const Status status = character.load(bytes, length, limits);
  heap_caps_free(bytes);         // Model owns its decoded arrays after load.
  if (status == Status::Ok) Serial.printf("Loaded %u bytes; input buffer released\n", unsigned(length));
  return status;
}

void setup() {
  Serial.begin(115200);
  Serial.println(kBoardName);
  Status status = loadFromLittleFS();
  if (status != Status::Ok) {
    Serial.printf("Model load: %s / %s\n", statusString(status), character.error());
    return;
  }
  scene.add(character.scene()); scene.add(ambient); scene.add(sunlight);
  sunlight.direction.set(-0.7f, -1, -0.5f).normalize();
  const Vector3 center(demo_asset::centerX, demo_asset::centerY, demo_asset::centerZ);
  const float radius = std::max(0.01f, demo_asset::radius);
  camera.position = center + Vector3(1.8f, 0.7f, 2.6f) * radius;
  camera.near = radius * 0.02f; camera.far = radius * 30;
  camera.updateProjectionMatrix(); camera.lookAt(center);
  const auto* clip = character.animation(0);
  if (!clip) { Serial.println("Model has no playable animation"); return; }
  action = &mixer.clipAction(*clip);
  action->setLoop(LoopRepeat).play();
  Serial.printf("Clip: %s (%.3f s)\n", clip->name, double(clip->duration));

  if (!display.begin()) { Serial.println("Display initialization failed"); return; }
  display.fillScreen(0);
  auto* backend = nice_gfx::frameBackend(display);
  if (!backend) { Serial.println("Display has no frame backend"); return; }
  RendererOptions options;
  options.width = 320; options.height = 240; options.clearColor = 0x1083;
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
  Serial.println("p pause/resume | r restart | +/- speed");
  animationClock.start(); lastFrame = lastReport = millis(); ready = true;
}

void loop() {
  if (!ready) { delay(100); return; }
  while (Serial.available()) {
    const char command = char(Serial.read());
    if(command=='h')orbitControls.reset();
    else if (command == 'p') action->paused = !action->paused;
    else if (command == 'r') action->reset().play();
    else if (command == '+' || command == '-') {
      playbackSpeed = std::max(0.25f, std::min(2.0f, playbackSpeed + (command == '+' ? 0.25f : -0.25f)));
      action->setEffectiveTimeScale(playbackSpeed);
      Serial.printf("Speed %.2fx\n", double(playbackSpeed));
    }
  }
  const uint32_t now = millis();
  orbitInput.update(orbitControls,now);
  if (uint32_t(now - lastFrame) < 16) { delay(1); return; }
  lastFrame = now;
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
