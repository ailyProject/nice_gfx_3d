#pragma once
#include "Animation.h"
#include "ModelCacheWorkspace.h"
#include "Status.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace nice_gfx_3d {

// Limits are authoring safeguards, not a promise that a board can allocate the
// largest permitted asset. Lower them to fit the application's memory budget.
struct ModelLimits {
  size_t bytes=64u*1024u*1024u;
  uint32_t vertices=262144,indices=786432,nodes=4096,meshes=4096,primitives=8192;
  uint32_t textures=256,texturePixels=16777216,skins=1024,jointsPerSkin=1024;
  uint32_t animations=1024,channels=8192,keysPerChannel=1048576,hierarchyDepth=128;
};

struct ModelPrimitive {
  uint32_t vertexStart=0,vertexCount=0,indexStart=0,indexCount=0;
  int32_t material=-1;
  uint32_t mesh=0,skinStart=0xffffffffu;
};
struct ModelMesh {uint32_t firstPrimitive=0,primitiveCount=0;};
struct ModelNode {
  int32_t parent=-1,mesh=-1,skin=-1;
  const char* name=nullptr;
  Vector3 position,scale{1,1,1};
  Quaternion quaternion;
};
struct ModelMaterial {
  float baseColor[4]={1,1,1,1};
  int32_t texture=-1;
  uint32_t flags=0;
  float alphaCutoff=.5f;
};
struct ModelTexture {
  uint32_t width=0,height=0,pixelOffset=0,alphaOffset=0,samplerFlags=0;
};
struct ModelSkin {
  uint32_t jointsOffset=0,jointCount=0,inverseBindOffset=0;
  int32_t skeletonRoot=-1;
  const char* name=nullptr;
};
struct ModelAnimation {uint32_t firstChannel=0,channelCount=0;const char* name=nullptr;};
struct ModelChannel {
  uint32_t node=0,keyCount=0,timesOffset=0,valuesOffset=0;
  TrackProperty property=TrackProperty::Position;
  Interpolation interpolation=Interpolation::Linear;
};

// Zero-allocation, endian/alignment-safe reader. It borrows the source bytes;
// do not mutate/free those bytes until every accessor and Model::load finishes.
// Failed parses invalidate the view; error() is a static diagnostic string.
class ModelView {
 public:
  Status parse(const void* data,size_t length,const ModelLimits& limits=ModelLimits());
  bool valid()const{return status_==Status::Ok;}
  Status status()const{return status_;}
  const char* error()const{return error_;}
  const uint8_t* data()const{return data_;}
  size_t size()const{return size_;}
  uint32_t vertexCount()const{return count(Vertices);}
  uint32_t skinVertexCount()const{return count(SkinVertices);}
  uint32_t indexCount()const{return count(Indices);}
  uint32_t meshCount()const{return count(Meshes);}
  uint32_t primitiveCount()const{return count(Primitives);}
  uint32_t nodeCount()const{return count(Nodes);}
  uint32_t materialCount()const{return count(Materials);}
  uint32_t textureCount()const{return count(Textures);}
  uint32_t skinCount()const{return count(Skins);}
  uint32_t animationCount()const{return count(Animations);}
  uint32_t channelCount()const{return count(Channels);}
  uint32_t rootCount()const{return count(Roots);}
  Vertex vertex(uint32_t index)const;
  SkinVertex skinVertex(uint32_t index)const;
  uint32_t index(uint32_t index)const;
  ModelMesh mesh(uint32_t index)const;
  ModelPrimitive primitive(uint32_t index)const;
  ModelNode node(uint32_t index)const;
  ModelMaterial material(uint32_t index)const;
  ModelTexture texture(uint32_t index)const;
  ModelSkin skin(uint32_t index)const;
  ModelAnimation animation(uint32_t index)const;
  ModelChannel channel(uint32_t index)const;
  uint32_t root(uint32_t index)const;
  uint32_t joint(const ModelSkin&,uint32_t index)const;
  Matrix4 inverseBindMatrix(const ModelSkin&,uint32_t index)const;
  uint16_t texturePixel(const ModelTexture&,uint32_t index)const;
  uint8_t textureAlpha(const ModelTexture&,uint32_t index)const;
  float keyTime(const ModelChannel&,uint32_t key)const;
  float keyValue(const ModelChannel&,uint32_t key,uint32_t component)const;
 private:
  enum SectionId:uint8_t {Vertices,SkinVertices,Indices,Meshes,Primitives,Nodes,Materials,Textures,Skins,Animations,Channels,Roots,SectionCount};
  struct Section {uint32_t offset=0,count=0,stride=0;};
  const uint8_t* data_=nullptr;
  size_t size_=0,payloadStart_=0;
  Section sections_[SectionCount];
  Status status_=Status::NotInitialized;
  const char* error_="view is not initialized";
  uint16_t u16(size_t at)const;
  uint32_t u32(size_t at)const;
  int32_t i32(size_t at)const;
  float f32(size_t at)const;
  uint32_t count(SectionId section)const{return valid()?sections_[section].count:0;}
  size_t record(SectionId section,uint32_t index)const;
  bool span(size_t at,size_t count,size_t stride=1)const;
  bool payload(size_t at,size_t count,size_t stride=1)const;
  bool nameValid(uint32_t at,uint32_t length)const;
  Status reject(Status code,const char* reason);
};

// Owned scene instance. load() performs setup allocations and deep-copies all
// borrowed input. Rendering and animation updates do not allocate model data.
// Destroy mixers/actions before clear/reload/destruction: they borrow clips and
// node targets. Scene links are non-owning; the stable scene() group may be
// attached to an application scene, and clear() detaches its former children.
class Model {
 public:
  Model();
  ~Model();
  Model(const Model&)=delete;Model& operator=(const Model&)=delete;
  Model(Model&&)=delete;Model& operator=(Model&&)=delete;
  Status load(const void* data,size_t length,const ModelLimits& limits=ModelLimits());
  Status load(const ModelView& view);
  // Setup-only opt-in. Vertex/normal/colour/UV/skin arrays must remain immutable
  // until clearStaticGeometryOptimization(). Node/bone animation stays dynamic.
  // ESP32 maps/UVs prefer internal RAM; original-attribute caches are optional
  // internal-only storage. Failure preserves existing optimization.
  Status optimizeStaticGeometry();
  Status optimizeStaticGeometry(ModelCacheWorkspace& workspace);
  // Adopt successful ordinary caches without reallocating or touching payloads.
  // Use an empty workspace; clear/reload can then reuse its exact buffers.
  Status retainOptimizations(ModelCacheWorkspace& workspace);
  // Borrow immutable offline topology hints. Keep hints and geometry arrays
  // alive and unchanged until clearClosedSurfaceHints(), after renders join.
  Status setClosedSurfaceHints(size_t primitive,const ClosedSurfaceHints* hints);
  void clearClosedSurfaceHints();
  size_t closedSurfaceComponentCount()const;
  void clearStaticGeometryOptimization();
  size_t staticGeometryOptimizationBytes()const;
  size_t staticVertexAttributeCacheBytes()const;
  size_t staticVertexAttributeCacheMeshes()const;
  size_t staticVertexAttributeInternalBytes()const;
  // Setup-only opt-in. Public Texture pixels/stride/sample remain row-major.
  // Texture pixels must stay immutable until clearTextureOptimization(). Wait
  // for all renders before enabling, clearing or rebuilding these caches.
  // Allocation failure preserves every previous cache. RGB payload uses
  // PSRAM. Optional compact metadata prefers internal RAM; allocation failure
  // selects the exact ordinary raw cache. Palettes never quantize colours.
  Status optimizeTextures();
  Status optimizeTextures(ModelCacheWorkspace& workspace);
  void clearTextureOptimization();
  size_t textureOptimizationBytes()const;
  size_t textureCompactMetadataBytes()const;
  size_t textureTilePayloadBytes()const;
  size_t solidTextureTileCount()const;
  size_t paletteTextureTileCount()const;
  bool textureCompactMetadataInternal()const;
  void clear();
  bool loaded()const{return storage_!=nullptr;}
  Status status()const{return status_;}
  const char* error()const{return error_;}
  Group& scene(){return root_;}
  const Group& scene()const{return root_;}
  size_t nodeCount()const;
  Object3D* node(size_t index);
  const Object3D* node(size_t index)const;
  size_t animationCount()const;
  const AnimationClip* animation(size_t index)const;
  // Invalid indices return a shared empty clip; animation(index) is nullable.
  const AnimationClip& clip(size_t index)const;
  const std::vector<AnimationClip>& animations()const;
 private:
  struct Storage;
  Group root_;
  std::unique_ptr<Storage> storage_;
  Status status_=Status::NotInitialized;
  const char* error_="model is not initialized";
  Status loadOwned(const ModelView& view);
  Status optimizeStaticGeometryWithCache(const std::shared_ptr<detail::ModelCacheState>& cache);
  Status optimizeTexturesWithCache(const std::shared_ptr<detail::ModelCacheState>& cache);
};

// Three.js-style name for the cooked-asset loader. JSON/GLB conversion belongs
// to the desktop compiler; this loader only accepts NG3D bytes.
class NG3DLoader {
 public:
  Status load(const void* bytes,size_t length,Model& model,const ModelLimits& limits=ModelLimits())const{return model.load(bytes,length,limits);}
};
} // namespace nice_gfx_3d
