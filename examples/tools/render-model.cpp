// Desktop asset inspection utility. Uses the actual portable C++ Model,
// AnimationMixer and Renderer; it does not emulate ESP32 timing or peripherals.
#include <NICE_GFX_3D.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace nice_gfx_3d;

static int failure(const char *where,Status status,const char *details="") {
  std::fprintf(stderr,"%s: %s %s\n",where,statusString(status),details);return 1;
}

static bool bounds(Object3D &root,size_t capacity,Vector3 &low,Vector3 &high) {
  std::vector<Object3D *> nodes;nodes.reserve(capacity);nodes.push_back(&root);
  for(size_t i=0;i<nodes.size();++i) {
    nodes[i]->updateWorldMatrix(false,false);
    for(Object3D *child:nodes[i]->children)if(child) {
      if(nodes.size()>=capacity)return false;
      nodes.push_back(child);
    }
  }
  bool found=false;
  for(Object3D *node:nodes) {
    if(node->type()!=ObjectType::Mesh&&node->type()!=ObjectType::SkinnedMesh)continue;
    auto &mesh=static_cast<Mesh &>(*node);
    auto *skin=node->type()==ObjectType::SkinnedMesh?static_cast<SkinnedMesh *>(&mesh):nullptr;
    if(!mesh.geometry || (skin&&!skin->prepareSkinning()))return false;
    for(size_t i=0;i<mesh.geometry->vertexCount;++i) {
      const Vertex vertex=skin?skin->skinVertex(i,mesh.geometry->vertices[i]):mesh.geometry->vertices[i];
      Vector3 p=vertex.position;p.applyMatrix4(mesh.matrixWorld);
      if(!p.isFinite())return false;
      if(!found){low=high=p;found=true;}
      else {
        low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
      }
    }
  }
  return found;
}

int main(int argc,char **argv) {
  if(argc<3){std::fprintf(stderr,"Usage: render-model input.ng3d output.ppm [clip-index] [seconds] [yaw-degrees]\n");return 2;}
  const unsigned clipIndex=argc>3?unsigned(std::strtoul(argv[3],nullptr,10)):0;
  const float seconds=argc>4?std::strtof(argv[4],nullptr):0;
  const float yaw=argc>5?std::strtof(argv[5],nullptr):0;
  if(!std::isfinite(seconds)||seconds<0||!std::isfinite(yaw))return failure("arguments",Status::InvalidArgument);
  std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
  if(!input){std::fprintf(stderr,"Cannot open input: %s\n",argv[1]);return 1;}
  const auto fileSize=input.tellg();
  if(fileSize<=0||fileSize>64*1024*1024)return failure("input length",Status::InvalidData);
  std::vector<uint8_t> bytes(size_t(fileSize),uint8_t(0));input.seekg(0);
  if(!input.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size())))return failure("input read",Status::IoError);
  ModelView view;Status status=view.parse(bytes.data(),bytes.size());
  if(status!=Status::Ok)return failure("ModelView::parse",status,view.error());
  uint32_t maximumVertices=3,meshObjects=0,triangles=0,joints=0;
  for(uint32_t i=0;i<view.primitiveCount();++i){const auto primitive=view.primitive(i);maximumVertices=std::max(maximumVertices,primitive.vertexCount);triangles+=primitive.indexCount/3;}
  for(uint32_t i=0;i<view.nodeCount();++i){const auto node=view.node(i);if(node.mesh>=0)meshObjects+=view.mesh(uint32_t(node.mesh)).primitiveCount;}
  for(uint32_t i=0;i<view.skinCount();++i)joints+=view.skin(i).jointCount;
  const uint32_t maxObjects=view.nodeCount()+meshObjects+8;
  const uint32_t nodeCount=view.nodeCount(),vertexCount=view.vertexCount(),primitiveCount=view.primitiveCount();
  Model model;status=model.load(view);if(status!=Status::Ok)return failure("Model::load",status,model.error());
  // Deep-copy semantics: input bytes are intentionally released before use.
  bytes.clear();bytes.shrink_to_fit();
  Scene scene;Group pivot;
  AmbientLight ambient(Color(0xffffff),.5f);DirectionalLight sun(Color(0xffffff),.75f);
  sun.direction.set(-.4f,-.65f,-1).normalize();
  scene.add(pivot);scene.add(ambient);scene.add(sun);pivot.add(model.scene());
  AnimationMixer mixer(model.scene());
  if(const AnimationClip *clip=model.animation(clipIndex))mixer.clipAction(*clip).setLoop(LoopRepeat).play();
  else if(model.animationCount())return failure("clip index",Status::InvalidArgument);
  mixer.update(0);
  Vector3 low,high;if(!bounds(scene,maxObjects,low,high))return failure("initial bounds",Status::InvalidData);
  const Vector3 center=(low+high)*.5f;
  const float radius=std::max(1e-6f,(high-low).length()*.5f);
  model.scene().position.sub(center);pivot.rotation.y=degToRad(yaw);
  if(seconds>0)mixer.update(seconds);
  PerspectiveCamera camera(45,4.0f/3,std::max(radius*.01f,1e-7f),radius*20+1);
  camera.position.set(0,0,radius/std::sin(degToRad(22.5f))*1.2f);camera.lookAt(0,0,0);
  RendererOptions options;options.width=320;options.height=240;options.clearColor=Color(0x15202e);
  options.maxVertices=maximumVertices;options.maxObjects=maxObjects;
  Renderer renderer;status=renderer.begin(options);if(status!=Status::Ok)return failure("Renderer::begin",status);
  status=renderer.render(scene,camera);if(status!=Status::Ok)return failure("Renderer::render",status);
  std::ofstream output(argv[2],std::ios::binary);
  if(!output)return failure("output open",Status::IoError);
  const auto &frame=renderer.frame();output<<"P6\n"<<frame.width<<' '<<frame.height<<"\n255\n";
  for(size_t y=0;y<frame.height;++y)for(size_t x=0;x<frame.width;++x) {
    const uint16_t c=frame.pixels[y*frame.stride+x];
    const char rgb[3]={char((c>>11)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};
    output.write(rgb,3);
  }
  output.close();if(!output)return failure("output write",Status::IoError);
  const auto &stats=renderer.stats();
  if(!stats.shadedPixels)return failure("visible model pixels",Status::InvalidData,"empty frame; adjust pose/yaw/camera");
  std::printf("loaded=yes rendered=yes nodes=%u vertices=%u triangles=%u primitives=%u joints=%u clips=%zu clip=%u seconds=%.3f drawn=%u pixels=%u buffers=%zu scratch=%zu\n",
      nodeCount,vertexCount,triangles,primitiveCount,joints,model.animationCount(),clipIndex,double(seconds),stats.drawnTriangles,stats.shadedPixels,stats.bufferBytes,stats.scratchBytes);
  for(size_t i=0;i<model.animationCount();++i)std::printf("clip[%zu]=%s duration=%.3f\n",i,model.clip(i).name?model.clip(i).name:"",double(model.clip(i).duration));
  return 0;
}
