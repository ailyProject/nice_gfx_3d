#include "Hardware.h"
#include "FoxModel.h"
#include <NICE_GFX_3D.h>
#include <NiceGfxPresenter.h>
#include <NiceGfxOrbitInput.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <vector>

using namespace nice_gfx_3d;
constexpr const char *Tag="model_player";

// Setup-only traversal: update all branches before preparing skeletons. Do not
// recompute bounds or allocate this vector inside the animation loop.
static bool sceneBounds(Object3D &root,size_t capacity,Vector3 &low,Vector3 &high) {
  std::vector<Object3D *> objects;objects.reserve(capacity);objects.push_back(&root);
  for(size_t i=0;i<objects.size();++i) {
    objects[i]->updateWorldMatrix(false,false);
    for(Object3D *child:objects[i]->children)if(child) {
      if(objects.size()>=capacity)return false;
      objects.push_back(child);
    }
  }
  bool found=false;
  for(Object3D *object:objects) {
    if(object->type()!=ObjectType::Mesh&&object->type()!=ObjectType::SkinnedMesh)continue;
    auto &mesh=static_cast<Mesh &>(*object);
    auto *skin=object->type()==ObjectType::SkinnedMesh?static_cast<SkinnedMesh *>(&mesh):nullptr;
    if(!mesh.geometry||(skin&&!skin->prepareSkinning()))return false;
    for(size_t i=0;i<mesh.geometry->vertexCount;++i) {
      const Vertex vertex=skin?skin->skinVertex(i,mesh.geometry->vertices[i]):mesh.geometry->vertices[i];
      Vector3 p=vertex.position;p.applyMatrix4(mesh.matrixWorld);
      if(!p.isFinite())return false;
      if(!found){low=high=p;found=true;}
      else {low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}
    }
  }
  return found;
}

static void playModel() {
  // One owner task controls the model, mixer, renderer and display.
  auto &display=exampleDisplay();
  if(!display.begin()){ESP_LOGE(Tag,"Display initialization failed");return;}
  display.fillScreen(0);
  auto *backend=nice_gfx::frameBackend(display);
  if(!backend){ESP_LOGE(Tag,"No frame backend");return;}
  ModelView view;Status status=view.parse(kFoxModel,sizeof(kFoxModel));
  if(status!=Status::Ok){ESP_LOGE(Tag,"Asset: %s / %s",statusString(status),view.error());return;}
  RendererOptions options;options.width=320;options.height=240;options.clearColor=Color(0x15202e);
  options.maxVertices=3;
  uint32_t instances=0;
  for(uint32_t i=0;i<view.primitiveCount();++i)options.maxVertices=std::max(options.maxVertices,view.primitive(i).vertexCount);
  for(uint32_t i=0;i<view.nodeCount();++i)if(view.node(i).mesh>=0)instances+=view.mesh(uint32_t(view.node(i).mesh)).primitiveCount;
  options.maxObjects=view.nodeCount()+instances+8;
  Scene scene;Group pivot;Model model;
  AmbientLight ambient(Color(0xffffff),.5f);DirectionalLight sun(Color(0xffffff),.75f);
  scene.add(pivot);scene.add(ambient);scene.add(sun);sun.direction.set(-.4f,-.65f,-1).normalize();
  status=model.load(view);
  if(status!=Status::Ok){ESP_LOGE(Tag,"Load: %s / %s",statusString(status),model.error());return;}
  pivot.add(model.scene());
  AnimationMixer mixer(model.scene());
  struct RestPose {Object3D *node;Vector3 position,scale;Quaternion quaternion;};
  std::vector<RestPose> restPoses;restPoses.reserve(model.nodeCount());
  for(size_t i=0;i<model.nodeCount();++i){auto *node=model.node(i);node->updateMatrix();restPoses.push_back({node,node->position,node->scale,node->quaternion});}
  std::vector<AnimationAction *> actions;actions.reserve(model.animationCount());
  for(size_t i=0;i<model.animationCount();++i){actions.push_back(&mixer.clipAction(model.clip(i)));ESP_LOGI(Tag,"clip[%u] %s / %.3fs",unsigned(i),model.clip(i).name,double(model.clip(i).duration));}
  size_t active=0;bool loop=true,paused=false;
  if(!actions.empty())actions[active]->setLoop(LoopRepeat).play();
  mixer.update(0);
  Vector3 low,high;
  if(!sceneBounds(scene,options.maxObjects,low,high)){ESP_LOGE(Tag,"Cannot fit model");return;}
  const Vector3 center=(low+high)*.5f;const float radius=std::max(1e-6f,(high-low).length()*.5f);
  model.scene().scale.set(1/radius,1/radius,1/radius);
  model.scene().position=center*(-1/radius);
  PerspectiveCamera camera(45,4.0f/3,.01f,100);
  float yaw=degToRad(65),distance=3.2f,speed=1;
  camera.position.set(0,.1f,distance);camera.lookAt(0,0,0);pivot.rotation.y=yaw;
  // The model has been normalized around the origin; orbit about that center.
  OrbitControls controls(camera);controls.target.set(0,0,0);
  controls.minDistance=.4f;controls.maxDistance=12;
  controls.setViewport(display.width(),display.height());controls.saveState();
  NiceGfxOrbitInput orbitInput;
  if(!orbitInput.begin(display.width(),display.height()))ESP_LOGW(Tag,"Touch unavailable, status=%u; rendering continues",unsigned(orbitInput.status()));
  Renderer renderer;NiceGfxPresenter presenter;
  status=renderer.begin(options);if(status==Status::Ok)status=presenter.begin(*backend);
  if(status!=Status::Ok){ESP_LOGE(Tag,"Initialize: %s",statusString(status));return;}
  ESP_LOGI(Tag,"Buffers=%u scratch=%u bytes; internal render %ux%u",unsigned(renderer.stats().bufferBytes),unsigned(renderer.stats().scratchBytes),options.width,options.height);
  const int flags=fcntl(STDIN_FILENO,F_GETFL,0);
  const bool console=flags>=0&&fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)>=0;
  ESP_LOGI(Tag,"Drag to orbit, pinch to zoom; h camera reset; n/b clip, p pause, r restart, l loop/once, +/- speed, a/d model yaw, w/s zoom");
  if(!console)ESP_LOGW(Tag,"Nonblocking stdin unavailable; playback continues without serial commands");
  uint64_t previous=uint64_t(esp_timer_get_time()),lastLog=previous;uint32_t frames=0;
  TickType_t tick=xTaskGetTickCount();
  while(true) {
    orbitInput.update(controls,uint32_t(esp_timer_get_time()/1000));
    if(console){char commands[32];const ssize_t count=read(STDIN_FILENO,commands,sizeof(commands));
      for(ssize_t i=0;i<count;++i){const char c=commands[i];
        if((c=='n'||c=='b')&&!actions.empty()) {
          actions[active]->stop();active=(active+(c=='n'?1:actions.size()-1))%actions.size();
          actions[active]->setLoop(loop?LoopRepeat:LoopOnce).reset().play();paused=false;
          ESP_LOGI(Tag,"Selected %s",model.clip(active).name);
        }
        if(c=='p')paused=!paused;
        if(c=='r'&&!actions.empty()){actions[active]->reset().play();paused=false;}
        if(c=='l'&&!actions.empty()){loop=!loop;actions[active]->setLoop(loop?LoopRepeat:LoopOnce).reset().play();}
        if(c=='+')speed=std::min(2.f,speed+.1f);if(c=='-')speed=std::max(.1f,speed-.1f);
        if(c=='a')yaw-=.15f;if(c=='d')yaw+=.15f;
        if(c=='w')controls.dolly(.9f);if(c=='s')controls.dolly(1.1f);
        if(c=='h'||c=='H')controls.reset();
      }
    }
    const uint64_t now=uint64_t(esp_timer_get_time());
    const float dt=std::min(.1f,float(now-previous)*1e-6f);previous=now;
    for(const auto &pose:restPoses){pose.node->position=pose.position;pose.node->scale=pose.scale;pose.node->setQuaternion(pose.quaternion);}
    mixer.update(paused?0:dt*speed);
    // Keep the camera pose produced by touch; a/d rotates the model turntable.
    pivot.rotation.y=yaw;
    status=renderer.render(scene,camera);
    if(status==Status::Ok)status=presenter.presentFit(renderer.frame());
    if(status!=Status::Ok&&status!=Status::Busy){ESP_LOGE(Tag,"Frame: %s",statusString(status));break;}
    if(status==Status::Ok)++frames;
    if(now-lastLog>=2000000){ESP_LOGI(Tag,"published %.1f fps; render %.2fms; present %.2fms; PPA=%s",double(frames)*1000000/double(now-lastLog),double(renderer.stats().renderMicros)/1000,double(presenter.stats().presentMicros)/1000,presenter.stats().hardwareScaled?"yes":"no");frames=0;lastLog=now;}
    // Always yield at least once even when a heavy frame exceeds its budget.
    vTaskDelay(1);vTaskDelayUntil(&tick,pdMS_TO_TICKS(16));
  }
}

static void runPlayer(void *) {
  // Return through the C++ scope first so error exits release model/buffers.
  playModel();
  vTaskDelete(nullptr);
}

extern "C" void app_main() {
  if(xTaskCreate(runPlayer,"model_player",16384,nullptr,1,nullptr)!=pdPASS)ESP_LOGE(Tag,"Unable to allocate model player task");
}
