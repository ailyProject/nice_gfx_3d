#pragma once
#include "Math3D.h"
#include "ClosedSurfaces.h"
#include <cstdint>
#include <vector>

namespace nice_gfx_3d {
struct Vertex {Vector3 position;Vector3 normal;Vector2 uv;uint16_t color=0xffff;};
// Optional per-vertex skin data stays separate so ordinary meshes pay no skin cost.
struct SkinVertex {uint16_t joints[4]={0,0,0,0};float weights[4]={0,0,0,0};};
struct BoundingSphere {Vector3 center;float radius=-1;};
#define NICE_GFX_3D_HAS_STATIC_VERTEX_ATTRIBUTES 1
namespace detail {
struct StaticNormalColor {Vector3 normal;uint16_t color=0xffff,padding=0;};
static_assert(sizeof(Vector3)==12&&sizeof(StaticNormalColor)==16,"Original attribute cache layout");
struct StaticVertexReuse {
  const Vertex* vertices=nullptr;
  const SkinVertex* skinVertices=nullptr;
  size_t vertexCount=0;
  const uint32_t* positions=nullptr;
  const uint32_t* lighting=nullptr;
  const Vector2* uvs=nullptr;
  // Original representatives remain in low16; high16 addresses dense cached
  // attributes. UINT32_MAX means ordinary unpacked maps and original inputs.
  uint32_t sourceMask=UINT32_MAX;
  const Vector3* originalPositions=nullptr;
  const StaticNormalColor* originalNormalColors=nullptr;
  const uint16_t* rigidJoints=nullptr;
  uint32_t cachedPositions=0,cachedNormalColors=0,cachedInputBytes=0;
  // Setup facts about immutable cached attributes; no per-vertex colour map.
  float maximumBasicNormal=0;
  uint16_t constantVertexColor=0xffff,maximumRigidJoint=0;
  bool constantColorValid=false,boundedRigidNormals=false;
};
}

// Geometry borrows all arrays. Mutate/release them only outside render calls.
class BufferGeometry {
  friend class Model;
  friend class Renderer;
  const detail::StaticVertexReuse* staticReuse_=nullptr;
  const detail::ClosedSurfaceReuse* closedSurfaces_=nullptr;
 public:
  const Vertex* vertices=nullptr;
  size_t vertexCount=0;
  const uint32_t* indices=nullptr;
  size_t indexCount=0;
  const SkinVertex* skinVertices=nullptr;
  BoundingSphere boundingSphere;
  BufferGeometry()=default;
  BufferGeometry(const Vertex* v,size_t count,const uint32_t* i=nullptr,size_t countIndices=0,const SkinVertex* skin=nullptr):vertices(v),vertexCount(count),indices(i),indexCount(countIndices),skinVertices(skin){}
  // Copies are ordinary borrowed views. Model-owned optimization metadata stays
  // with its original geometry so disabling it cannot invalidate a copied view.
  BufferGeometry(const BufferGeometry& other){*this=other;}
  BufferGeometry& operator=(const BufferGeometry& other){
    if(this!=&other){
      vertices=other.vertices;vertexCount=other.vertexCount;
      indices=other.indices;indexCount=other.indexCount;
      skinVertices=other.skinVertices;boundingSphere=other.boundingSphere;
      staticReuse_=nullptr;closedSurfaces_=nullptr;
    }
    return *this;
  }
  virtual ~BufferGeometry()=default;
  // Borrowed views may be copied. Built-in owned subclasses explicitly forbid it.
  bool valid()const{return vertices&&vertexCount>=3&&((indices&&indexCount>=3&&indexCount%3==0)||(!indices&&indexCount==0&&vertexCount%3==0));}
  size_t triangleCount()const{return (indices?indexCount:vertexCount)/3;}
  void computeBoundingSphere(){
    boundingSphere={};if(!vertices||!vertexCount)return;
    Vector3 low=vertices[0].position,high=low;
    if(!low.isFinite())return;
    for(size_t i=1;i<vertexCount;++i){const auto&p=vertices[i].position;if(!p.isFinite())return;low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}
    boundingSphere.center=(low+high)*.5f;float r2=0;for(size_t i=0;i<vertexCount;++i)r2=std::max(r2,(vertices[i].position-boundingSphere.center).lengthSq());boundingSphere.radius=std::sqrt(r2);
  }
};
class BoxGeometry:public BufferGeometry {
  std::vector<Vertex> ownedVertices_;
  std::vector<uint32_t> ownedIndices_;
 public:
  explicit BoxGeometry(float width=1,float height=1,float depth=1){
    const float x=width*.5f,y=height*.5f,z=depth*.5f;
    const Vector3 positions[6][4]={
      {{x,-y,z},{x,-y,-z},{x,y,-z},{x,y,z}},
      {{-x,-y,-z},{-x,-y,z},{-x,y,z},{-x,y,-z}},
      {{-x,y,z},{x,y,z},{x,y,-z},{-x,y,-z}},
      {{-x,-y,-z},{x,-y,-z},{x,-y,z},{-x,-y,z}},
      {{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}},
      {{x,-y,-z},{-x,-y,-z},{-x,y,-z},{x,y,-z}}
    };
    const Vector3 normals[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const Vector2 uv[4]={{0,0},{1,0},{1,1},{0,1}};
    ownedVertices_.reserve(24);ownedIndices_.reserve(36);
    for(uint32_t face=0;face<6;++face){for(size_t j=0;j<4;++j)ownedVertices_.push_back({positions[face][j],normals[face],uv[j],0xffff});const uint32_t b=face*4;for(uint32_t i:{b,b+1,b+2,b,b+2,b+3})ownedIndices_.push_back(i);}
    vertices=ownedVertices_.data();vertexCount=ownedVertices_.size();indices=ownedIndices_.data();indexCount=ownedIndices_.size();computeBoundingSphere();
  }
  BoxGeometry(const BoxGeometry&)=delete;BoxGeometry&operator=(const BoxGeometry&)=delete;
  BoxGeometry(BoxGeometry&&)=delete;BoxGeometry&operator=(BoxGeometry&&)=delete;
};
class PlaneGeometry:public BufferGeometry {
  Vertex ownedVertices_[4];
  uint32_t ownedIndices_[6]={0,1,2,0,2,3};
 public:
  // XY plane, front normal +Z, counterclockwise winding when seen from +Z.
  explicit PlaneGeometry(float width=1,float height=1){const float x=width*.5f,y=height*.5f;ownedVertices_[0]={{-x,-y,0},{0,0,1},{0,0},0xffff};ownedVertices_[1]={{x,-y,0},{0,0,1},{1,0},0xffff};ownedVertices_[2]={{x,y,0},{0,0,1},{1,1},0xffff};ownedVertices_[3]={{-x,y,0},{0,0,1},{0,1},0xffff};vertices=ownedVertices_;vertexCount=4;indices=ownedIndices_;indexCount=6;computeBoundingSphere();}
  PlaneGeometry(const PlaneGeometry&)=delete;PlaneGeometry&operator=(const PlaneGeometry&)=delete;
  PlaneGeometry(PlaneGeometry&&)=delete;PlaneGeometry&operator=(PlaneGeometry&&)=delete;
};
} // namespace nice_gfx_3d
