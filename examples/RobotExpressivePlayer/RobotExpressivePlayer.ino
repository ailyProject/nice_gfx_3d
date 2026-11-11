#include "Hardware.h"
#include "RobotExpressiveData.h"
#include <NiceGfxOrbitInput.h>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace nice_gfx_3d;

Scene scene;
Model robot;
PerspectiveCamera camera(45,4.f/3.f,.01f,100.f);
OrbitControls orbitControls(camera);
NiceGfxOrbitInput orbitInput;
float modelRadius=1;
AmbientLight ambient(0xffff,.55f);
DirectionalLight sunlight(0xffff,.85f);
Renderer renderer;
NiceGfxPresenter presenter;

// All Model, clip, target and keyframe storage outlives the mixer/actions.
AnimationMixer mixer(robot.scene());
constexpr size_t MaxClips=32;
AnimationAction *actions[MaxClips]={};
float weights[MaxClips]={},fadeFrom[MaxClips]={};
size_t clipCount=0,targetClip=0;
float fadeTime=.6f,fadeElapsed=.6f,playbackSpeed=1,orbitAngle=0;
bool ready=false,paused=false,crossfade=true,loopCurrent=true,orbit=false,playlist=true;
uint32_t previousFrame=0,lastPlaylist=0;
char command[32]={};size_t commandLength=0;bool commandOverflow=false;

struct RestPose { Object3D *node;Vector3 position,scale;Quaternion quaternion; };
std::vector<RestPose> restPoses;

int findClip(const char *name) {
  for(size_t i=0;i<clipCount;++i)if(robot.clip(i).name&&std::strcmp(robot.clip(i).name,name)==0)return int(i);
  return -1;
}

void restoreRestPose() {
  // Source clips animate different node/property subsets. Start at the same
  // rest pose so a property absent from every active clip cannot stick at the
  // previous action's last value. No allocation or model reload in this loop.
  for(const auto &pose:restPoses){pose.node->position=pose.position;pose.node->scale=pose.scale;pose.node->setQuaternion(pose.quaternion);}
}

void printMenu() {
  Serial.println("\nRobotExpressive: enter a clip number, then Enter:");
  for(size_t i=0;i<clipCount;++i){Serial.print(i);Serial.print("  ");Serial.print(robot.clip(i).name);Serial.print("  ");Serial.print(robot.clip(i).duration,3);Serial.println(" s");}
  Serial.println("p pause | r restart | l repeat/once | c crossfade | a playlist");
  Serial.println("o turntable | h camera home | + faster | - slower | s status | m menu");
  Serial.println("Touch: one finger orbits the camera; pinch zooms. Touch takes over from the turntable.");
}

void printStatus() {
  Serial.print("clip=");Serial.print(robot.clip(targetClip).name);
  Serial.print("  paused=");Serial.print(paused);
  Serial.print("  repeat=");Serial.print(loopCurrent);
  Serial.print("  speed=");Serial.print(playbackSpeed,2);
  Serial.print("  playlist=");Serial.println(playlist);
  Serial.print("weights:");for(size_t i=0;i<clipCount;++i)if(weights[i]>.001f){Serial.print(' ');Serial.print(robot.clip(i).name);Serial.print('=');Serial.print(weights[i],3);}Serial.println();
  const auto &stats=renderer.stats();
  Serial.print("last render us=");Serial.print(static_cast<unsigned long>(stats.renderMicros));
  Serial.print("  triangles=");Serial.print(stats.triangles);
  Serial.print("  drawn=");Serial.println(stats.drawnTriangles);
}

bool repeatByDefault(size_t index) {
  const char *name=robot.clip(index).name;
  return std::strcmp(name,"Walking")==0||std::strcmp(name,"Running")==0||std::strcmp(name,"Idle")==0||std::strcmp(name,"Dance")==0;
}

void selectClip(size_t index,bool fade) {
  if(index>=clipCount)return;
  for(size_t i=0;i<clipCount;++i)fadeFrom[i]=weights[i];
  targetClip=index;loopCurrent=repeatByDefault(index);
  actions[index]->reset().setLoop(loopCurrent?LoopRepeat:LoopOnce).play();
  actions[index]->paused=paused;
  fadeElapsed=fade&&crossfade?0:fadeTime;
  if(fadeElapsed>=fadeTime)for(size_t i=0;i<clipCount;++i)weights[i]=i==targetClip?1.f:0.f;
  Serial.print("Selected: ");Serial.println(robot.clip(index).name);
}

bool fitCamera() {
  // Setup-only parent-first walk. Finish all matrices before evaluating skins.
  std::vector<Object3D *> objects;objects.reserve(robot_expressive_max_objects);
  objects.push_back(&scene);
  for(size_t i=0;i<objects.size();++i){auto *object=objects[i];object->updateWorldMatrix(false,false);
    for(auto *child:object->children)if(child){if(objects.size()>=robot_expressive_max_objects)return false;objects.push_back(child);}}
  bool found=false;Vector3 low,high;
  for(auto *object:objects)if(object->type()==ObjectType::Mesh||object->type()==ObjectType::SkinnedMesh){
    auto &mesh=static_cast<Mesh &>(*object);
    auto *skin=object->type()==ObjectType::SkinnedMesh?static_cast<SkinnedMesh *>(&mesh):nullptr;
    if(skin&&!skin->prepareSkinning())return false;
    if(!mesh.geometry)continue;
    for(size_t i=0;i<mesh.geometry->vertexCount;++i){const Vertex vertex=skin?skin->skinVertex(i,mesh.geometry->vertices[i]):mesh.geometry->vertices[i];Vector3 p=vertex.position;p.applyMatrix4(mesh.matrixWorld);if(!p.isFinite())return false;
      if(!found){low=high=p;found=true;}else{low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}}
  }
  if(!found)return false;
  const Vector3 center=(low+high)*.5f;const float radius=std::max(.001f,(high-low).length()*.5f);
  modelRadius=radius;
  robot.scene().position.sub(center);
  // Extra margin accommodates Jump/WalkJump and the horizontal Death pose.
  const float distance=radius/std::sin(22.5f*3.14159265f/180.f)*1.5f;
  camera.near=std::max(.001f,radius*.005f);camera.far=distance+radius*12;
  camera.updateProjectionMatrix();camera.position.set(0,radius*.08f,distance);camera.lookAt(0,0,0);
  return true;
}

void processCommand() {
  if(!commandLength)return;
  command[commandLength]='\0';
  if(std::strcmp(command,"m")==0||std::strcmp(command,"?")==0)printMenu();
  else if(std::strcmp(command,"s")==0)printStatus();
  else if(std::strcmp(command,"p")==0){paused=!paused;for(size_t i=0;i<clipCount;++i)actions[i]->paused=paused;if(!paused)lastPlaylist=millis();Serial.println(paused?"Paused":"Playing");}
  else if(std::strcmp(command,"r")==0){restoreRestPose();for(size_t i=0;i<clipCount;++i){actions[i]->reset().play();actions[i]->paused=paused;weights[i]=i==targetClip?1.f:0.f;}fadeElapsed=fadeTime;lastPlaylist=millis();}
  else if(std::strcmp(command,"l")==0){loopCurrent=!loopCurrent;actions[targetClip]->setLoop(loopCurrent?LoopRepeat:LoopOnce).reset().play();actions[targetClip]->paused=paused;Serial.println(loopCurrent?"Repeat":"Once, hold final pose");}
  else if(std::strcmp(command,"c")==0){crossfade=!crossfade;Serial.println(crossfade?"Crossfade 0.6 s":"Immediate clip switches");}
  else if(std::strcmp(command,"a")==0){playlist=!playlist;lastPlaylist=millis();Serial.println(playlist?"Playlist enabled":"Playlist disabled");}
  else if(std::strcmp(command,"o")==0)orbit=!orbit;
  else if(std::strcmp(command,"h")==0)orbitControls.reset();
  else if(std::strcmp(command,"+")==0)playbackSpeed=std::min(2.f,playbackSpeed+.25f);
  else if(std::strcmp(command,"-")==0)playbackSpeed=std::max(.25f,playbackSpeed-.25f);
  else {char *end=nullptr;const long index=std::strtol(command,&end,10);if(end!=command&&*end=='\0'&&index>=0&&size_t(index)<clipCount){playlist=false;selectClip(size_t(index),true);}else Serial.println("Unknown command; enter m for menu");}
}

void pollSerial() {
  // Bounded input processing; no String allocations and no blocking readLine.
  for(size_t count=0;count<64&&Serial.available();++count){const char c=char(Serial.read());
    if(c=='\n'||c=='\r'){if(commandOverflow)Serial.println("Command too long");else processCommand();commandLength=0;commandOverflow=false;}
    else if(c>=32&&c<127){if(commandLength+1<sizeof(command))command[commandLength++]=c;else commandOverflow=true;}}
}

void setup() {
  Serial.begin(115200);
  const Status loaded=robot.load(robot_expressive_ng3d,robot_expressive_ng3d_size);
  if(loaded!=Status::Ok){Serial.print("Model load failed: ");Serial.println(robot.error());return;}
  clipCount=robot.animationCount();if(!clipCount||clipCount>MaxClips){Serial.println("Unsupported animation count");return;}
  scene.add(robot.scene());scene.add(ambient);scene.add(sunlight);sunlight.direction.set(-1,-1,-1).normalize();
  restPoses.reserve(robot.nodeCount());
  for(size_t i=0;i<robot.nodeCount();++i){auto *node=robot.node(i);node->updateMatrix();restPoses.push_back({node,node->position,node->scale,node->quaternion});}
  // Register every action once, while all targets still have their rest pose.
  for(size_t i=0;i<clipCount;++i){actions[i]=&mixer.clipAction(robot.clip(i));actions[i]->clampWhenFinished=true;actions[i]->weight=0;actions[i]->play();if(!actions[i]->valid()){Serial.println("Invalid animation clip");return;}}
  if(!fitCamera()){Serial.println("Unable to fit model bounds");return;}
  if(!robot_player_hardware::begin(renderer,presenter,robot_expressive_max_vertices,robot_expressive_max_objects))return;
  auto& screen=robot_player_hardware::display();
  // fitCamera already moved the asset center to world origin.
  orbitControls.target.set(0,0,0);
  orbitControls.minDistance=modelRadius*2.2f;orbitControls.maxDistance=modelRadius*12;
  orbitControls.setViewport(screen.width(),screen.height());orbitControls.saveState();
  const bool touchReady=orbitInput.begin(screen.width(),screen.height());
  Serial.printf("Orbit touch probe: %s (status %u); drag=orbit, pinch=zoom, h=home\n",touchReady?"ready":"unavailable",unsigned(orbitInput.status()));
  const int walking=findClip("Walking");selectClip(walking>=0?size_t(walking):0,false);
  printMenu();previousFrame=lastPlaylist=millis();ready=true;
}

void loop() {
  if(!ready){delay(100);return;}
  pollSerial();const uint32_t now=millis();
  orbitInput.update(orbitControls,now);
  if(orbitInput.lastContactCount())orbit=false; // Keep the user's camera view.
  if(uint32_t(now-previousFrame)<16){delay(1);return;}
  const float seconds=std::min(.1f,float(uint32_t(now-previousFrame))*.001f);previousFrame=now;
  if(!paused&&playlist&&uint32_t(now-lastPlaylist)>=5000){
    static const char *const names[]={"Walking","Running","Jump","Dance","Wave","Idle"};
    static size_t next=1;const int index=findClip(names[next]);next=(next+1)%6;
    if(index>=0)selectClip(size_t(index),true);lastPlaylist=now;
  }
  if(!paused&&fadeElapsed<fadeTime){fadeElapsed=std::min(fadeTime,fadeElapsed+seconds);const float t=fadeElapsed/fadeTime,s=t*t*(3-2*t);for(size_t i=0;i<clipCount;++i)weights[i]=fadeFrom[i]*(1-s)+(i==targetClip?s:0);}
  for(size_t i=0;i<clipCount;++i)actions[i]->setEffectiveWeight(weights[i]);
  restoreRestPose();mixer.timeScale=playbackSpeed;mixer.update(paused?0:seconds);
  if(orbit&&!paused)orbitAngle+=seconds*.25f;robot.scene().rotation.y=orbitAngle;
  Status result=renderer.render(scene,camera);
  if(result==Status::Ok)result=presenter.presentFit(renderer.frame());
  // A temporarily busy display can retry on the next scheduled frame. Other
  // failures stop this demo instead of repeatedly flooding the serial output.
  if(result!=Status::Ok&&result!=Status::Busy){Serial.println(statusString(result));ready=false;}
  // Complex frames may exceed 16 ms, bypassing the interval's early delay.
  // Always let the idle task and other firmware work run after a frame.
  delay(1);
}
