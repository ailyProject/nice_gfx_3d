#include "Model.h"
#include "Memory.h"
#include "ModelCacheWorkspaceState.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#if NICE_GFX_3D_ESP
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_memory_utils.h"
#endif

namespace nice_gfx_3d {
namespace {
constexpr uint32_t strides[12]={36,24,4,8,32,60,28,24,24,16,24,4};
constexpr const char* tags[12]={"VERT","VSKN","INDX","MESH","PRIM","NODE","MATL","TEXR","SKIN","ANIM","CHAN","ROOT"};
bool indexValid(int32_t index,uint32_t count){return index>=-1&&(index<0||uint32_t(index)<count);}
bool subrange(uint32_t first,uint32_t count,uint32_t total){return first<=total&&count<=total-first;}
bool finiteFloats(const float* values,size_t count){for(size_t i=0;i<count;++i)if(!std::isfinite(values[i]))return false;return true;}
uint16_t color565(const float* c){return uint16_t((uint16_t(clamp(c[0],0,1)*31+.5f)<<11)|(uint16_t(clamp(c[1],0,1)*63+.5f)<<5)|uint16_t(clamp(c[2],0,1)*31+.5f));}

void* allocateStaticMaps(size_t bytes){
#if NICE_GFX_3D_ESP
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return detail::allocate(bytes,MemoryPreference::ExternalFirst,alignof(void*));
#endif
}
uint32_t floatBits(float value){
  uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits;
}
uint32_t reuseHash(const BufferGeometry& geometry,uint32_t index,bool lighting){
  const auto& vertex=geometry.vertices[index];
  const auto& vector=lighting?vertex.normal:vertex.position;
  uint32_t hash=2166136261u;
  const auto mix=[&](uint32_t value){hash=(hash^value)*16777619u;};
  mix(floatBits(vector.x));mix(floatBits(vector.y));mix(floatBits(vector.z));
  if(lighting)mix(vertex.color);
  if(geometry.skinVertices){
    const auto& skin=geometry.skinVertices[index];
    for(size_t i=0;i<4;++i){mix(skin.joints[i]);mix(floatBits(skin.weights[i]));}
  }
  return hash;
}
bool sameReuseInput(const BufferGeometry& geometry,uint32_t a,uint32_t b,bool lighting){
  const auto& first=geometry.vertices[a];const auto& second=geometry.vertices[b];
  const auto& x=lighting?first.normal:first.position;
  const auto& y=lighting?second.normal:second.position;
  if(floatBits(x.x)!=floatBits(y.x)||floatBits(x.y)!=floatBits(y.y)||floatBits(x.z)!=floatBits(y.z))return false;
  if(lighting&&first.color!=second.color)return false;
  if(geometry.skinVertices){
    const auto& sa=geometry.skinVertices[a];const auto& sb=geometry.skinVertices[b];
    for(size_t i=0;i<4;++i)if(sa.joints[i]!=sb.joints[i]||floatBits(sa.weights[i])!=floatBits(sb.weights[i]))return false;
  }
  return true;
}
void packAttributeReuseMap(uint32_t* mapping,size_t count){
  uint32_t dense=0;
  for(uint32_t i=0;i<count;++i){
    const uint32_t source=mapping[i];
    const uint32_t cacheIndex=source==i?dense++:mapping[source]>>16;
    mapping[i]=source|(cacheIndex<<16);
  }
}
void buildReuseMap(const BufferGeometry& geometry,bool lighting,uint32_t* mapping,uint32_t* buckets,size_t bucketCount){
  std::memset(buckets,0xff,bucketCount*sizeof(uint32_t));
  for(uint32_t i=0;i<geometry.vertexCount;++i){
    size_t slot=reuseHash(geometry,i,lighting)&(bucketCount-1);
    while(buckets[slot]!=UINT32_MAX&&!sameReuseInput(geometry,i,buckets[slot],lighting))slot=(slot+1)&(bucketCount-1);
    if(buckets[slot]==UINT32_MAX)buckets[slot]=i;
    mapping[i]=buckets[slot];
  }
}
}

uint16_t ModelView::u16(size_t at)const{return uint16_t(data_[at])|(uint16_t(data_[at+1])<<8);}
uint32_t ModelView::u32(size_t at)const{return uint32_t(data_[at])|(uint32_t(data_[at+1])<<8)|(uint32_t(data_[at+2])<<16)|(uint32_t(data_[at+3])<<24);}
int32_t ModelView::i32(size_t at)const{const uint32_t n=u32(at);int32_t result;std::memcpy(&result,&n,4);return result;}
float ModelView::f32(size_t at)const{const uint32_t n=u32(at);float result;static_assert(sizeof(result)==4,"NG3D requires float32");std::memcpy(&result,&n,4);return result;}
bool ModelView::span(size_t at,size_t count,size_t stride)const{return at<=size_&&stride&&count<=(size_-at)/stride;}
bool ModelView::payload(size_t at,size_t count,size_t stride)const{return at>=payloadStart_&&(at%4)==0&&span(at,count,stride);}
bool ModelView::nameValid(uint32_t at,uint32_t length)const{if(at<payloadStart_||at%4||at>=size_||size_t(length)>=size_-at||data_[size_t(at)+length]!=0)return false;for(size_t i=0;i<length;++i)if(data_[size_t(at)+i]==0)return false;return true;}
Status ModelView::reject(Status code,const char* reason){status_=code;error_=reason;return code;}
size_t ModelView::record(SectionId section,uint32_t index)const{return valid()&&index<sections_[section].count?size_t(sections_[section].offset)+size_t(index)*sections_[section].stride:0;}

Status ModelView::parse(const void* data,size_t length,const ModelLimits& limits){
  data_=static_cast<const uint8_t*>(data);size_=length;payloadStart_=0;status_=Status::NotInitialized;error_="view is not initialized";
  for(auto& section:sections_)section=Section();
  if(!data||length<32)return reject(Status::InvalidArgument,"NG3D input is null or shorter than its header");
  if(length>limits.bytes)return reject(Status::CapacityExceeded,"NG3D byte budget exceeded");
  if(std::memcmp(data_,"NG3D",4)!=0)return reject(Status::InvalidData,"NG3D magic is missing");
  if(u16(4)!=1||u16(6)!=32)return reject(Status::Unsupported,"NG3D version/header size is unsupported");
  if(u32(8)!=length)return reject(Status::InvalidData,"NG3D declared length differs from input length");
  if(u32(12)!=SectionCount||u32(20)||u32(24)||u32(28))return reject(Status::Unsupported,"NG3D header contains unsupported sections or flags");
  const uint32_t directory=u32(16);
  if(directory<32||directory%4||!span(directory,SectionCount,16))return reject(Status::InvalidData,"NG3D directory is out of bounds");
  payloadStart_=size_t(directory)+SectionCount*16;
  bool seen[SectionCount]={};
  for(size_t i=0;i<SectionCount;++i){
    const size_t at=directory+i*16;size_t id=0;while(id<SectionCount&&std::memcmp(data_+at,tags[id],4)!=0)++id;
    if(id==SectionCount||seen[id])return reject(Status::Unsupported,"NG3D directory has an unknown or duplicate section");
    seen[id]=true;auto& section=sections_[id];section.count=u32(at+4);section.stride=u32(at+8);section.offset=u32(at+12);
    if(section.stride!=strides[id])return reject(Status::Unsupported,"NG3D record stride is unsupported");
    if(section.offset<payloadStart_||section.offset%4||!span(section.offset,section.count,section.stride))return reject(Status::InvalidData,"NG3D section is out of bounds or overlaps the directory");
  }
  for(size_t i=0;i<SectionCount;++i){
    const auto& a=sections_[i];const size_t end=size_t(a.offset)+size_t(a.count)*a.stride;payloadStart_=std::max(payloadStart_,end);
    if(!a.count)continue;
    for(size_t j=0;j<i;++j){const auto& b=sections_[j];if(b.count&&size_t(a.offset)<size_t(b.offset)+size_t(b.count)*b.stride&&size_t(b.offset)<end)return reject(Status::InvalidData,"NG3D fixed sections overlap");}
  }
  const uint32_t budgets[SectionCount]={limits.vertices,limits.vertices,limits.indices,limits.meshes,limits.primitives,limits.nodes,limits.primitives,limits.textures,limits.skins,limits.animations,limits.channels,limits.nodes};
  for(size_t i=0;i<SectionCount;++i)if(sections_[i].count>budgets[i])return reject(Status::CapacityExceeded,"NG3D record count budget exceeded");
  // Accessors are now range-safe, but the view remains uncommitted on failure.
  status_=Status::Ok;
  for(uint32_t i=0;i<vertexCount();++i){const size_t at=record(Vertices,i);for(size_t c=0;c<8;++c)if(!std::isfinite(f32(at+c*4)))return reject(Status::InvalidData,"NG3D vertex has a nonfinite component");if(u16(at+34))return reject(Status::Unsupported,"NG3D vertex has unsupported flags");}
  for(uint32_t i=0;i<skinVertexCount();++i){const auto s=skinVertex(i);float total=0;for(float w:s.weights){if(!std::isfinite(w)||w<0)return reject(Status::InvalidData,"NG3D skin weight is invalid");total+=w;}if(!std::isfinite(total)||std::fabs(total-1)>1e-3f)return reject(Status::InvalidData,"NG3D skin weights must sum to one");}
  for(uint32_t i=0;i<primitiveCount();++i){
    const auto p=primitive(i);if(!p.vertexCount||!p.indexCount||p.indexCount%3||!subrange(p.vertexStart,p.vertexCount,vertexCount())||!subrange(p.indexStart,p.indexCount,indexCount())||!indexValid(p.material,materialCount())||p.mesh>=meshCount())return reject(Status::InvalidData,"NG3D primitive ranges or references are invalid");
    if(p.skinStart!=0xffffffffu&&!subrange(p.skinStart,p.vertexCount,skinVertexCount()))return reject(Status::InvalidData,"NG3D primitive skin range is invalid");
    if(u32(record(Primitives,i)+28))return reject(Status::Unsupported,"NG3D primitive flags are unsupported");
    for(uint32_t j=0;j<p.indexCount;++j)if(index(p.indexStart+j)>=p.vertexCount)return reject(Status::InvalidData,"NG3D triangle index exceeds primitive vertices");
  }
  for(uint32_t i=0;i<meshCount();++i){const auto m=mesh(i);if(!m.primitiveCount||!subrange(m.firstPrimitive,m.primitiveCount,primitiveCount()))return reject(Status::InvalidData,"NG3D mesh primitive range is invalid");for(uint32_t j=0;j<m.primitiveCount;++j)if(primitive(m.firstPrimitive+j).mesh!=i)return reject(Status::InvalidData,"NG3D primitive mesh reference is inconsistent");}
  for(uint32_t i=0;i<nodeCount();++i){
    const size_t at=record(Nodes,i);const auto n=node(i);
    if(!indexValid(n.parent,nodeCount())||n.parent==int32_t(i)||!indexValid(n.mesh,meshCount())||!indexValid(n.skin,skinCount())||(n.skin>=0&&n.mesh<0)||!nameValid(u32(at+12),u32(at+16)))return reject(Status::InvalidData,"NG3D node references or name are invalid");
    for(size_t c=0;c<10;++c)if(!std::isfinite(f32(at+20+c*4)))return reject(Status::InvalidData,"NG3D node transform is nonfinite");
    const float q=n.quaternion.dot(n.quaternion);if(!std::isfinite(q)||q<1e-12f)return reject(Status::InvalidData,"NG3D node quaternion has zero or invalid length");
  }
  for(uint32_t i=0;i<nodeCount();++i){int32_t p=int32_t(i);uint32_t depth=0;while(p>=0){if(++depth>nodeCount())return reject(Status::InvalidData,"NG3D hierarchy has a cycle");if(depth>limits.hierarchyDepth)return reject(Status::CapacityExceeded,"NG3D hierarchy depth budget exceeded");p=node(uint32_t(p)).parent;}}
  for(uint32_t i=0;i<rootCount();++i){const uint32_t n=root(i);if(n>=nodeCount()||node(n).parent!=-1)return reject(Status::InvalidData,"NG3D active scene root is invalid");for(uint32_t j=0;j<i;++j)if(root(j)==n)return reject(Status::InvalidData,"NG3D active scene root is duplicated");}
  for(uint32_t i=0;i<materialCount();++i){const auto m=material(i);if(!finiteFloats(m.baseColor,4)||!std::isfinite(m.alphaCutoff)||m.alphaCutoff<0||m.alphaCutoff>1||!indexValid(m.texture,textureCount()))return reject(Status::InvalidData,"NG3D material references or factors are invalid");for(float c:m.baseColor)if(c<0||c>1)return reject(Status::InvalidData,"NG3D base color is outside zero to one");if(m.flags&~7u||(m.flags&6u)==6u)return reject(Status::Unsupported,"NG3D material flags are unsupported");}
  uint64_t totalPixels=0;
  for(uint32_t i=0;i<textureCount();++i){
    const auto t=texture(i);if(!t.width||!t.height||t.width>4096||t.height>4096)return reject(Status::InvalidData,"NG3D texture dimensions are invalid");
    const uint64_t pixels=uint64_t(t.width)*t.height;totalPixels+=pixels;if(totalPixels>limits.texturePixels)return reject(Status::CapacityExceeded,"NG3D texture pixel budget exceeded");
    if(!payload(t.pixelOffset,size_t(pixels),2)||(t.alphaOffset&&!payload(t.alphaOffset,size_t(pixels))))return reject(Status::InvalidData,"NG3D texture pixels are out of bounds");
    if((t.samplerFlags&~15u)||(t.samplerFlags&3u)>2u||((t.samplerFlags>>2)&3u)>2u||u32(record(Textures,i)+20))return reject(Status::Unsupported,"NG3D texture sampler is unsupported");
  }
  for(uint32_t i=0;i<skinCount();++i){
    const auto s=skin(i);const size_t at=record(Skins,i);
    if(!s.jointCount||s.jointCount>65536||!indexValid(s.skeletonRoot,nodeCount())||!payload(s.jointsOffset,s.jointCount,4)||!payload(s.inverseBindOffset,s.jointCount,64)||!nameValid(u32(at+16),u32(at+20)))return reject(Status::InvalidData,"NG3D skeleton payload is invalid");
    if(s.jointCount>limits.jointsPerSkin)return reject(Status::CapacityExceeded,"NG3D joint count budget exceeded");
    for(uint32_t j=0;j<s.jointCount;++j){
      if(joint(s,j)>=nodeCount())return reject(Status::InvalidData,"NG3D skeleton refers to an invalid node");
      for(uint32_t k=0;k<j;++k)if(joint(s,j)==joint(s,k))return reject(Status::InvalidData,"NG3D skeleton joint is duplicated");
      auto m=inverseBindMatrix(s,j);if(!finiteFloats(m.elements,16))return reject(Status::InvalidData,"NG3D inverse bind matrix is nonfinite");
      const auto* e=m.elements;
      if(std::fabs(e[3])+std::fabs(e[7])+std::fabs(e[11])+std::fabs(e[15]-1)>1e-5f||!m.tryInvert())return reject(Status::InvalidData,"NG3D inverse bind matrix must be affine and invertible");
    }
  }
  const auto activeNode=[this](uint32_t index){int32_t top=int32_t(index);while(node(uint32_t(top)).parent>=0)top=node(uint32_t(top)).parent;for(uint32_t i=0;i<rootCount();++i)if(root(i)==uint32_t(top))return true;return false;};
  for(uint32_t i=0;i<nodeCount();++i){
    const auto n=node(i);if(n.skin<0)continue;const auto s=skin(uint32_t(n.skin));
    if(activeNode(i))for(uint32_t j=0;j<s.jointCount;++j)if(!activeNode(joint(s,j)))return reject(Status::Unsupported,"NG3D active skinned mesh refers to a joint outside the active scene");
    const auto m=mesh(uint32_t(n.mesh));for(uint32_t j=0;j<m.primitiveCount;++j){const auto p=primitive(m.firstPrimitive+j);if(p.skinStart==0xffffffffu)return reject(Status::InvalidData,"NG3D skinned node lacks vertex joint data");for(uint32_t k=0;k<p.vertexCount;++k){const auto v=skinVertex(p.skinStart+k);for(uint16_t jointIndex:v.joints)if(jointIndex>=s.jointCount)return reject(Status::InvalidData,"NG3D vertex joint exceeds its skeleton");}}
  }
  for(uint32_t i=0;i<channelCount();++i){
    const size_t at=record(Channels,i);const uint32_t property=u32(at+4),interpolation=u32(at+8);if(property>2||interpolation>1)return reject(Status::Unsupported,"NG3D animation target or interpolation is unsupported");
    const auto c=channel(i);const size_t components=property==1?4:3;
    if(c.node>=nodeCount()||!c.keyCount||!payload(c.timesOffset,c.keyCount,4)||!payload(c.valuesOffset,c.keyCount,components*4))return reject(Status::InvalidData,"NG3D animation channel payload is invalid");
    if(c.keyCount>limits.keysPerChannel)return reject(Status::CapacityExceeded,"NG3D keyframe budget exceeded");
    float previous=-1;for(uint32_t k=0;k<c.keyCount;++k){const float time=keyTime(c,k);if(!std::isfinite(time)||time<0||time<=previous)return reject(Status::InvalidData,"NG3D animation times must be finite and strictly increasing");previous=time;float squared=0;for(uint32_t j=0;j<components;++j){const float value=keyValue(c,k,j);if(!std::isfinite(value))return reject(Status::InvalidData,"NG3D keyframe value is nonfinite");squared+=value*value;}if(property==1&&(!std::isfinite(squared)||squared<1e-12f))return reject(Status::InvalidData,"NG3D rotation key has zero or invalid length");}
  }
  for(uint32_t i=0;i<animationCount();++i){const auto a=animation(i);const size_t at=record(Animations,i);if(!a.channelCount||!subrange(a.firstChannel,a.channelCount,channelCount())||!nameValid(u32(at+8),u32(at+12)))return reject(Status::InvalidData,"NG3D animation range or name is invalid");for(uint32_t j=0;j<a.channelCount;++j)for(uint32_t k=0;k<j;++k){const auto x=channel(a.firstChannel+j),y=channel(a.firstChannel+k);if(x.node==y.node&&x.property==y.property)return reject(Status::InvalidData,"NG3D animation has duplicate target channels");}}
  error_="ok";return Status::Ok;
}

Vertex ModelView::vertex(uint32_t i)const{Vertex result;const size_t at=record(Vertices,i);if(!at)return result;result.position={f32(at),f32(at+4),f32(at+8)};result.normal={f32(at+12),f32(at+16),f32(at+20)};result.uv={f32(at+24),f32(at+28)};result.color=u16(at+32);return result;}
SkinVertex ModelView::skinVertex(uint32_t i)const{SkinVertex result;const size_t at=record(SkinVertices,i);if(!at)return result;for(size_t j=0;j<4;++j){result.joints[j]=u16(at+j*2);result.weights[j]=f32(at+8+j*4);}return result;}
uint32_t ModelView::index(uint32_t i)const{const size_t at=record(Indices,i);return at?u32(at):0;}
ModelMesh ModelView::mesh(uint32_t i)const{ModelMesh result;const size_t at=record(Meshes,i);if(at){result.firstPrimitive=u32(at);result.primitiveCount=u32(at+4);}return result;}
ModelPrimitive ModelView::primitive(uint32_t i)const{ModelPrimitive result;const size_t at=record(Primitives,i);if(at){result.vertexStart=u32(at);result.vertexCount=u32(at+4);result.indexStart=u32(at+8);result.indexCount=u32(at+12);result.material=i32(at+16);result.mesh=u32(at+20);result.skinStart=u32(at+24);}return result;}
ModelNode ModelView::node(uint32_t i)const{ModelNode result;const size_t at=record(Nodes,i);if(at){result.parent=i32(at);result.mesh=i32(at+4);result.skin=i32(at+8);const uint32_t offset=u32(at+12);if(span(offset,1))result.name=reinterpret_cast<const char*>(data_+offset);result.position={f32(at+20),f32(at+24),f32(at+28)};result.quaternion={f32(at+32),f32(at+36),f32(at+40),f32(at+44)};result.scale={f32(at+48),f32(at+52),f32(at+56)};}return result;}
ModelMaterial ModelView::material(uint32_t i)const{ModelMaterial result;const size_t at=record(Materials,i);if(at){for(size_t j=0;j<4;++j)result.baseColor[j]=f32(at+j*4);result.texture=i32(at+16);result.flags=u32(at+20);result.alphaCutoff=f32(at+24);}return result;}
ModelTexture ModelView::texture(uint32_t i)const{ModelTexture result;const size_t at=record(Textures,i);if(at){result.width=u32(at);result.height=u32(at+4);result.pixelOffset=u32(at+8);result.alphaOffset=u32(at+12);result.samplerFlags=u32(at+16);}return result;}
ModelSkin ModelView::skin(uint32_t i)const{ModelSkin result;const size_t at=record(Skins,i);if(at){result.jointsOffset=u32(at);result.jointCount=u32(at+4);result.inverseBindOffset=u32(at+8);result.skeletonRoot=i32(at+12);const uint32_t offset=u32(at+16);if(span(offset,1))result.name=reinterpret_cast<const char*>(data_+offset);}return result;}
ModelAnimation ModelView::animation(uint32_t i)const{ModelAnimation result;const size_t at=record(Animations,i);if(at){result.firstChannel=u32(at);result.channelCount=u32(at+4);const uint32_t offset=u32(at+8);if(span(offset,1))result.name=reinterpret_cast<const char*>(data_+offset);}return result;}
ModelChannel ModelView::channel(uint32_t i)const{ModelChannel result;const size_t at=record(Channels,i);if(at){result.node=u32(at);result.property=TrackProperty(u32(at+4));result.interpolation=Interpolation(u32(at+8));result.keyCount=u32(at+12);result.timesOffset=u32(at+16);result.valuesOffset=u32(at+20);}return result;}
uint32_t ModelView::root(uint32_t i)const{const size_t at=record(Roots,i);return at?u32(at):0;}
uint32_t ModelView::joint(const ModelSkin& s,uint32_t i)const{const size_t at=size_t(s.jointsOffset)+size_t(i)*4;return valid()&&i<s.jointCount&&span(at,1,4)?u32(at):0;}
Matrix4 ModelView::inverseBindMatrix(const ModelSkin& s,uint32_t i)const{Matrix4 result;const size_t at=size_t(s.inverseBindOffset)+size_t(i)*64;if(valid()&&i<s.jointCount&&span(at,16,4))for(size_t j=0;j<16;++j)result.elements[j]=f32(at+j*4);return result;}
uint16_t ModelView::texturePixel(const ModelTexture& t,uint32_t i)const{const size_t at=size_t(t.pixelOffset)+size_t(i)*2;return valid()&&uint64_t(i)<uint64_t(t.width)*t.height&&span(at,1,2)?u16(at):uint16_t(0xffff);}
uint8_t ModelView::textureAlpha(const ModelTexture& t,uint32_t i)const{const size_t at=size_t(t.alphaOffset)+i;return valid()&&t.alphaOffset&&uint64_t(i)<uint64_t(t.width)*t.height&&span(at,1)?data_[at]:uint8_t(255);}
float ModelView::keyTime(const ModelChannel& c,uint32_t key)const{const size_t at=size_t(c.timesOffset)+size_t(key)*4;return valid()&&key<c.keyCount&&span(at,1,4)?f32(at):0;}
float ModelView::keyValue(const ModelChannel& c,uint32_t key,uint32_t component)const{const size_t components=c.property==TrackProperty::Quaternion?4:3;const size_t at=size_t(c.valuesOffset)+(size_t(key)*components+component)*4;return valid()&&key<c.keyCount&&component<components&&span(at,1,4)?f32(at):0;}

struct Model::Storage {
  // Small animation channels can collectively consume megabytes. Place their
  // owned key arrays in PSRAM first, preserving internal RAM for RTOS objects.
  // Explicit allocation failure lets loadOwned return without replacing a model.
  class TrackBuffer {
   public:
    TrackBuffer()=default;
    TrackBuffer(const TrackBuffer&)=delete;
    TrackBuffer& operator=(const TrackBuffer&)=delete;
    TrackBuffer(TrackBuffer&& other) noexcept:values_(other.values_){other.values_=nullptr;}
    TrackBuffer& operator=(TrackBuffer&& other) noexcept {
      if(this!=&other){detail::release(values_);values_=other.values_;other.values_=nullptr;}
      return *this;
    }
    ~TrackBuffer(){detail::release(values_);}
    bool allocate(size_t count){
      if(count>SIZE_MAX/sizeof(float))return false;
      auto* values=static_cast<float*>(detail::allocate(count*sizeof(float),
        MemoryPreference::ExternalFirst,alignof(void*)));
      if(!values)return false;
      detail::release(values_);values_=values;return true;
    }
    float* data(){return values_;}
   private:
    float* values_=nullptr;
  };
  using OwnedTilePixels=std::unique_ptr<uint16_t,detail::ModelCacheDeleter>;
  using OwnedTileBlocks=std::unique_ptr<uint32_t,detail::ModelCacheDeleter>;
  struct TextureData {
    std::vector<uint16_t> pixels;
    std::vector<uint8_t> alpha;
    Texture texture;
    OwnedTilePixels tiledPixels{nullptr,detail::ModelCacheDeleter{}};
    OwnedTileBlocks compactBlocks{nullptr,detail::ModelCacheDeleter{}};
    detail::TextureTiles tiles;
  };
  struct TrackData {TrackBuffer times,values;};
  std::vector<Vertex> vertices;
  std::vector<SkinVertex> skinVertices;
  std::vector<uint32_t> indices;
  std::vector<BufferGeometry> geometries;
  std::shared_ptr<detail::ModelCacheState> textureCacheState;
  std::vector<TextureData> textures;
  size_t textureTileBytes=0;
  std::vector<MeshLambertMaterial> materials;
  std::vector<std::string> nodeNames,clipNames;
  std::vector<std::unique_ptr<Object3D>> nodes;
  std::vector<std::unique_ptr<Mesh>> meshInstances;
  std::vector<std::unique_ptr<Skeleton>> skeletons;
  std::vector<TrackData> trackData;
  std::vector<KeyframeTrack> tracks;
  std::vector<AnimationClip> clips;
  struct OwnedStaticAttributes {
    void* positions=nullptr;
    void* normalColors=nullptr;
    void* rigidJoints=nullptr;
    detail::ModelCacheState* cacheState=nullptr;
    ~OwnedStaticAttributes(){detail::ModelCacheDeleter release{cacheState};release(positions);release(normalColors);release(rigidJoints);}
  };
  uint32_t* staticMaps=nullptr;
  std::shared_ptr<detail::ModelCacheState> staticCacheState;
  std::unique_ptr<OwnedStaticAttributes[]> staticAttributes;
  size_t staticAttributeBytes=0,staticAttributeMeshes=0;
  size_t staticMapBytes=0;
  std::unique_ptr<detail::StaticVertexReuse[]> staticReuse;
  std::unique_ptr<detail::ClosedSurfaceReuse[]> closedReuse;
  // Disconnect before vector destruction, regardless of glTF node ordering.
  ~Storage(){
    detail::ModelCacheDeleter{staticCacheState.get()}(staticMaps);
    // A caller may have reparented a node into an external scene. Unlink while
    // every owned object is alive so no parent retains a dangling child.
    for(auto& object:meshInstances)if(object&&object->parent)object->parent->remove(*object);
    for(auto& object:nodes)if(object&&object->parent)object->parent->remove(*object);
    for(auto& object:meshInstances)if(object){for(auto* child:object->children)if(child&&child->parent==object.get())child->parent=nullptr;object->children.clear();}
    for(auto& object:nodes)if(object){for(auto* child:object->children)if(child&&child->parent==object.get())child->parent=nullptr;object->children.clear();}
  }
};

void Model::clearClosedSurfaceHints(){
  if(!storage_)return;
  for(auto& geometry:storage_->geometries)geometry.closedSurfaces_=nullptr;
  storage_->closedReuse.reset();
}
size_t Model::closedSurfaceComponentCount()const{
  size_t count=0;
  if(storage_)for(const auto& geometry:storage_->geometries)
    if(geometry.closedSurfaces_)count+=geometry.closedSurfaces_->hints->componentCount;
  return count;
}
Status Model::setClosedSurfaceHints(size_t primitive,const ClosedSurfaceHints* hints){
  if(!storage_){status_=Status::NotInitialized;error_="model is not initialized";return status_;}
  if(primitive>=storage_->geometries.size()){status_=Status::InvalidArgument;error_="closed-surface primitive is invalid";return status_;}
  auto& geometry=storage_->geometries[primitive];
  if(!hints){geometry.closedSurfaces_=nullptr;status_=Status::Ok;error_="ok";return status_;}
  if(hints->vertexCount!=geometry.vertexCount||hints->triangleCount!=geometry.triangleCount()||
      hints->componentCount>128||!hints->componentCount||!hints->components||!hints->triangleComponents){
    status_=Status::InvalidData;error_="closed-surface hints do not match geometry";return status_;
  }
  for(size_t i=0;i<hints->triangleCount;++i){
    const uint16_t component=hints->triangleComponents[i];
    if(component!=UINT16_MAX&&component>=hints->componentCount){status_=Status::InvalidData;error_="closed-surface triangle reference is invalid";return status_;}
  }
  for(size_t i=0;i<hints->componentCount;++i){
    const auto& component=hints->components[i];
    if(!component.center.isFinite()||!component.halfExtent.isFinite()||component.halfExtent.x<0||component.halfExtent.y<0||component.halfExtent.z<0||
        (component.winding!=1&&component.winding!=-1)||
        ((component.joint==UINT32_MAX)!=(geometry.skinVertices==nullptr))){
      status_=Status::InvalidData;error_="closed-surface component is invalid";return status_;
    }
  }
  if(!storage_->closedReuse){
    std::unique_ptr<detail::ClosedSurfaceReuse[]> pending(new(std::nothrow) detail::ClosedSurfaceReuse[storage_->geometries.size()]);
    if(!pending){status_=Status::NoMemory;error_="not enough memory for closed-surface descriptors";return status_;}
    storage_->closedReuse=std::move(pending);
  }
  auto& descriptor=storage_->closedReuse[primitive];
  descriptor={hints,geometry.vertices,geometry.skinVertices,geometry.indices,geometry.vertexCount,geometry.indexCount};
  geometry.closedSurfaces_=&descriptor;
  status_=Status::Ok;error_="ok";return status_;
}

Model::Model()=default;
Model::~Model(){clear();}
void Model::clearStaticGeometryOptimization(){
  if(!storage_)return;
  for(auto& geometry:storage_->geometries)geometry.staticReuse_=nullptr;
  storage_->staticReuse.reset();
  storage_->staticAttributes.reset();
  storage_->staticAttributeBytes=storage_->staticAttributeMeshes=0;
  detail::ModelCacheDeleter{storage_->staticCacheState.get()}(storage_->staticMaps);storage_->staticMaps=nullptr;
  storage_->staticCacheState.reset();
  storage_->staticMapBytes=0;
}
size_t Model::staticGeometryOptimizationBytes()const{
  return storage_?storage_->staticMapBytes:0;
}
size_t Model::staticVertexAttributeCacheBytes()const{return storage_?storage_->staticAttributeBytes:0;}
size_t Model::staticVertexAttributeCacheMeshes()const{return storage_?storage_->staticAttributeMeshes:0;}
size_t Model::staticVertexAttributeInternalBytes()const{
  size_t bytes=0;
#if NICE_GFX_3D_ESP
  if(storage_&&storage_->staticReuse)for(size_t i=0;i<storage_->geometries.size();++i){
    const auto& cache=storage_->staticReuse[i];
    if(cache.originalPositions&&esp_ptr_internal(cache.originalPositions))bytes+=cache.cachedInputBytes;
  }
#endif
  return bytes;
}
void Model::clearTextureOptimization(){
  if(!storage_)return;
  for(auto& owned:storage_->textures){
    owned.texture.tiles_=nullptr;
    owned.tiledPixels.reset();owned.compactBlocks.reset();owned.tiles={};
  }
  storage_->textureTileBytes=0;storage_->textureCacheState.reset();
}
size_t Model::textureOptimizationBytes()const{
  return storage_?storage_->textureTileBytes:0;
}
size_t Model::textureCompactMetadataBytes()const{
  size_t total=0;if(storage_)for(const auto& owned:storage_->textures)
    if(owned.compactBlocks)total+=size_t(owned.tiles.columns)*((size_t(owned.tiles.height)+7)/8)*sizeof(uint32_t);
  return total;
}
size_t Model::textureTilePayloadBytes()const{
  size_t total=0;if(storage_)for(const auto& owned:storage_->textures)total+=owned.tiles.payloadBytes;return total;
}
size_t Model::solidTextureTileCount()const{
  size_t total=0;if(storage_)for(const auto& owned:storage_->textures)total+=owned.tiles.solidTileCount;return total;
}
size_t Model::paletteTextureTileCount()const{
  size_t total=0;if(storage_)for(const auto& owned:storage_->textures)total+=owned.tiles.paletteTileCount;return total;
}
bool Model::textureCompactMetadataInternal()const{
#if NICE_GFX_3D_ESP
  bool present=false;if(storage_)for(const auto& owned:storage_->textures)if(owned.compactBlocks){
    present=true;if(!esp_ptr_internal(owned.compactBlocks.get()))return false;
  }
  return present;
#else
  return false;
#endif
}
Status Model::optimizeTextures(){return optimizeTexturesWithCache({});}
Status Model::optimizeTextures(ModelCacheWorkspace& workspace){
  if(!workspace.state_)workspace.state_=detail::ModelCacheState::create();
  if(!workspace.state_){status_=Status::NoMemory;error_="not enough memory for cache workspace";return status_;}
  return optimizeTexturesWithCache(workspace.state_);
}
Status Model::optimizeTexturesWithCache(const std::shared_ptr<detail::ModelCacheState>& cache){
  if(cache&&cache->active&&cache->owner!=this){status_=Status::Busy;error_="cache workspace belongs to another model";return status_;}
  if(!storage_){status_=Status::NotInitialized;error_="model is not initialized";return status_;}
  if(cache&&storage_->textureCacheState==cache&&storage_->textureTileBytes){
    bool unchanged=true;
    for(const auto& owned:storage_->textures){const auto& t=owned.texture;const auto& old=owned.tiles;
      if(!owned.tiledPixels||old.sourcePixels!=t.pixels||old.width!=t.width||old.height!=t.height||old.stride!=t.stride){unchanged=false;break;}
    }
    if(unchanged){for(auto& owned:storage_->textures)owned.texture.tiles_=&owned.tiles;status_=Status::Ok;error_="ok";return status_;}
    status_=Status::Busy;error_="clear texture caches before changing workspace sources";return status_;
  }
  detail::ModelCacheTransaction transaction(cache);
  struct Pending {
    Storage::OwnedTilePixels pixels{nullptr,detail::ModelCacheDeleter{}};
    Storage::OwnedTileBlocks compactBlocks{nullptr,detail::ModelCacheDeleter{}};
    detail::TextureTiles tiles;
  };
  const size_t count=storage_->textures.size();
  if(!count){clearTextureOptimization();status_=Status::Ok;error_="ok";return status_;}
  std::unique_ptr<Pending[]> pending(new(std::nothrow) Pending[count]);
  if(!pending){status_=Status::NoMemory;error_="not enough memory for texture cache descriptors";return status_;}
  size_t total=0;
  for(size_t i=0;i<count;++i){
    const auto& texture=storage_->textures[i].texture;
    if(!texture.valid()||texture.height>SIZE_MAX/sizeof(uint16_t)/texture.stride){
      status_=Status::InvalidData;error_="texture descriptor is invalid";return status_;
    }
    const size_t columns=(size_t(texture.width)+7)/8,rows=(size_t(texture.height)+7)/8;
    if(columns>UINT32_MAX||rows>SIZE_MAX/128/columns){
      status_=Status::CapacityExceeded;error_="tiled texture size exceeds capacity";return status_;
    }
    const size_t blockCount=columns*rows,rawBytes=blockCount*128,blockBytes=blockCount*sizeof(uint32_t);
    size_t bytes=rawBytes,metadataBytes=0;uint32_t solidCount=0,paletteCount=0;
    // Tags reserve the top2bits; enormous64-bit-host textures retain the raw
    // path rather than truncating a payload offset or diagnostic tile count.
    auto* blocks=blockCount<=UINT32_MAX&&rawBytes/64<=0x3fffffffu?
      static_cast<uint32_t*>(cache?cache->acquire(this,detail::ModelCacheKind::TextureBlocks,i,blockBytes,MemoryPreference::InternalFirst,64):detail::allocate(blockBytes,MemoryPreference::InternalFirst,64)):nullptr;
    if(blocks){
      pending[i].compactBlocks.get_deleter().state=cache.get();pending[i].compactBlocks.reset(blocks);bytes=0;
      for(size_t by=0;by<rows;++by)for(size_t bx=0;bx<columns;++bx){
        const size_t x0=bx*8,y0=by*8,x1=std::min(x0+8,size_t(texture.width)),y1=std::min(y0+8,size_t(texture.height));
        uint16_t palette[16];unsigned colors=0;
        for(size_t y=y0;y<y1&&colors<=16;++y)for(size_t x=x0;x<x1;++x){
          const uint16_t color=texture.pixels[y*texture.stride+x];unsigned index=0;
          for(;index<colors&&index<16&&palette[index]!=color;++index){}
          if(index==colors){if(colors<16)palette[colors]=color;++colors;}
          if(colors>16)break;
        }
        if(colors==1){blocks[by*columns+bx]=0x80000000u|uint32_t(palette[0]);++solidCount;}
        else{blocks[by*columns+bx]=uint32_t(bytes>>6)|(colors<=16?0x40000000u:0u);bytes+=colors<=16?64:128;paletteCount+=colors<=16;}
      }
      if((solidCount||paletteCount)&&blockBytes<=rawBytes-bytes){metadataBytes=blockBytes;}
      else{pending[i].compactBlocks.reset();blocks=nullptr;bytes=rawBytes;solidCount=paletteCount=0;}
    }
    if(bytes>SIZE_MAX-total||metadataBytes>SIZE_MAX-total-bytes){status_=Status::CapacityExceeded;error_="texture cache total exceeds capacity";return status_;}
    // All-solid textures need no payload reads, but keep a valid owned base for
    // the shared tiled descriptor. Account for its one64-byte allocation.
    const size_t payloadBytes=bytes?bytes:64;
    if(payloadBytes>SIZE_MAX-total-metadataBytes){status_=Status::CapacityExceeded;error_="texture cache total exceeds capacity";return status_;}
    uint16_t* pixels=nullptr;
    if(cache)pixels=static_cast<uint16_t*>(cache->acquire(this,detail::ModelCacheKind::TexturePixels,i,payloadBytes,MemoryPreference::ExternalFirst,64));
    else{
#if NICE_GFX_3D_ESP
      pixels=static_cast<uint16_t*>(heap_caps_aligned_alloc(64,payloadBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
      pixels=static_cast<uint16_t*>(detail::allocate(payloadBytes,MemoryPreference::ExternalFirst,64));
#endif
    }
    if(!pixels){status_=cache&&cache->lastStatus!=Status::Ok?cache->lastStatus:Status::NoMemory;error_="not enough memory or matching workspace capacity for tiled textures";return status_;}
    pending[i].pixels.get_deleter().state=cache.get();pending[i].pixels.reset(pixels);std::memset(pixels,0,payloadBytes);
    if(blocks){
      auto* payload=reinterpret_cast<uint8_t*>(pixels);
      for(size_t by=0;by<rows;++by)for(size_t bx=0;bx<columns;++bx){
        const uint32_t block=blocks[by*columns+bx];if(block&0x80000000u)continue;
        const size_t x0=bx*8,y0=by*8,x1=std::min(x0+8,size_t(texture.width)),y1=std::min(y0+8,size_t(texture.height));
        uint8_t* data=payload+(size_t(block&0x3fffffffu)<<6);
        if(block&0x40000000u){
          auto* palette=reinterpret_cast<uint16_t*>(data+32);unsigned colors=0;
          for(size_t y=y0;y<y1;++y)for(size_t x=x0;x<x1;++x){
            const uint16_t color=texture.pixels[y*texture.stride+x];unsigned index=0;
            for(;index<colors&&palette[index]!=color;++index){}
            if(index==colors)palette[colors++]=color;
            const unsigned at=unsigned((y-y0)*8+x-x0);
            data[at>>1]|=uint8_t(index<<((at&1u)*4));
          }
        }else{
          auto* raw=reinterpret_cast<uint16_t*>(data);
          for(size_t y=y0;y<y1;++y)for(size_t x=x0;x<x1;++x)raw[(y-y0)*8+x-x0]=texture.pixels[y*texture.stride+x];
        }
      }
    }else{
      for(uint32_t y=0;y<texture.height;++y)for(uint32_t x=0;x<texture.width;++x){
        const size_t tile=(size_t(y>>3)*columns+(x>>3))*64;
        pixels[tile+size_t(y&7)*8+(x&7)]=texture.pixels[size_t(y)*texture.stride+x];
      }
    }
    pending[i].tiles={texture.pixels,pixels,texture.width,texture.height,texture.stride,uint32_t(columns)};
    pending[i].tiles.compactBlocks=blocks;pending[i].tiles.payloadBytes=payloadBytes;
    pending[i].tiles.solidTileCount=solidCount;pending[i].tiles.paletteTileCount=paletteCount;
    total+=payloadBytes+metadataBytes;
  }
  // Keep the previous lease state alive while its deleters are replaced.
  const auto oldCacheState=storage_->textureCacheState;storage_->textureCacheState=cache;
  // Publish only complete, owned caches. Earlier tables remain live on error.
  for(size_t i=0;i<count;++i){
    auto& owned=storage_->textures[i];owned.texture.tiles_=nullptr;
    owned.tiledPixels=std::move(pending[i].pixels);owned.compactBlocks=std::move(pending[i].compactBlocks);owned.tiles=pending[i].tiles;
    owned.texture.tiles_=&owned.tiles;
  }
  storage_->textureTileBytes=total;transaction.commit(true);status_=Status::Ok;error_="ok";return status_;
}
Status Model::optimizeStaticGeometry(){return optimizeStaticGeometryWithCache({});}
Status Model::optimizeStaticGeometry(ModelCacheWorkspace& workspace){
  if(!workspace.state_)workspace.state_=detail::ModelCacheState::create();
  if(!workspace.state_){status_=Status::NoMemory;error_="not enough memory for cache workspace";return status_;}
  return optimizeStaticGeometryWithCache(workspace.state_);
}
Status Model::optimizeStaticGeometryWithCache(const std::shared_ptr<detail::ModelCacheState>& cache){
  if(cache&&cache->active&&cache->owner!=this){status_=Status::Busy;error_="cache workspace belongs to another model";return status_;}
  if(!storage_){status_=Status::NotInitialized;error_="model is not initialized";return status_;}
  size_t total=0,maximum=0;
  for(const auto& geometry:storage_->geometries){
    if(!geometry.vertices||geometry.vertexCount>UINT32_MAX||geometry.vertexCount>SIZE_MAX/16-total){
      status_=Status::CapacityExceeded;error_="static geometry map size exceeds capacity";return status_;
    }
    total+=geometry.vertexCount;maximum=std::max(maximum,geometry.vertexCount);
  }
  if(!total){clearStaticGeometryOptimization();status_=Status::Ok;error_="ok";return status_;}
  if(maximum>SIZE_MAX/2||storage_->geometries.size()>(SIZE_MAX-total*16)/sizeof(detail::StaticVertexReuse)){
    status_=Status::CapacityExceeded;error_="static geometry table size exceeds capacity";return status_;
  }
  // Repeated setup on the same immutable sources needs no replacement peak.
  // Capacity/reference validation above still occurs before this no-op.
  bool unchanged=storage_->staticReuse&&(storage_->staticAttributeBytes||(cache&&storage_->staticCacheState==cache))&&(!cache||storage_->staticCacheState==cache);
  if(unchanged)for(size_t i=0;i<storage_->geometries.size();++i){
    const auto& geometry=storage_->geometries[i];const auto& old=storage_->staticReuse[i];
    if(old.vertices!=geometry.vertices||old.skinVertices!=geometry.skinVertices||old.vertexCount!=geometry.vertexCount){unchanged=false;break;}
  }
  if(unchanged){
    // An ordinary geometry copy/assignment deliberately clears its private
    // metadata. Explicit Model reoptimization can restore the owned binding
    // after every immutable source reference/count has matched above.
    for(size_t i=0;i<storage_->geometries.size();++i)
      storage_->geometries[i].staticReuse_=&storage_->staticReuse[i];
    status_=Status::Ok;error_="ok";return status_;
  }
  size_t buckets=1;
  while(buckets<maximum*2){
    if(buckets>SIZE_MAX/2){status_=Status::CapacityExceeded;error_="static geometry table size exceeds capacity";return status_;}
    buckets*=2;
  }
  if(buckets>SIZE_MAX/sizeof(uint32_t)){status_=Status::CapacityExceeded;error_="static geometry table size exceeds capacity";return status_;}
  detail::ModelCacheTransaction transaction(cache);
  using OwnedMaps=std::unique_ptr<uint32_t,detail::ModelCacheDeleter>;
  static_assert(sizeof(Vector2)==8,"Hot UV layout requires two floats");
  MemoryPreference mapPreference=MemoryPreference::InternalFirst;
#if NICE_GFX_3D_ESP
  // Existing optional caches must not push replacement hot maps into PSRAM.
  // Keep the old optimization intact if an INTERNAL replacement cannot fit.
  if(storage_->staticAttributeBytes&&storage_->staticMaps&&esp_ptr_internal(storage_->staticMaps))mapPreference=MemoryPreference::InternalOnly;
#endif
  OwnedMaps maps(static_cast<uint32_t*>(cache?cache->acquire(this,detail::ModelCacheKind::Maps,0,total*16,mapPreference,128):detail::allocate(total*16,mapPreference)),detail::ModelCacheDeleter{cache.get()});
  OwnedMaps table(static_cast<uint32_t*>(allocateStaticMaps(buckets*sizeof(uint32_t))),detail::ModelCacheDeleter{});
  std::unique_ptr<detail::StaticVertexReuse[]> metadata(new(std::nothrow) detail::StaticVertexReuse[storage_->geometries.size()]);
  if(!maps||!table||!metadata){status_=!maps&&cache&&cache->lastStatus!=Status::Ok?cache->lastStatus:Status::NoMemory;error_="not enough memory or matching workspace capacity for static geometry maps";return status_;}
  // Ownership descriptors and each input block are optional. Allocate after the
  // original hot maps; failure leaves that geometry's maps wholly unpacked.
  std::unique_ptr<Storage::OwnedStaticAttributes[]> attributes(new(std::nothrow) Storage::OwnedStaticAttributes[storage_->geometries.size()]);
  size_t attributeBytes=0,attributeMeshes=0;
  size_t offset=0,uvOffset=0;
  auto* cachedUvs=reinterpret_cast<Vector2*>(maps.get()+total*2);
  for(size_t i=0;i<storage_->geometries.size();++i){
    const auto& geometry=storage_->geometries[i];auto& reuse=metadata[i];
    reuse.vertices=geometry.vertices;reuse.skinVertices=geometry.skinVertices;reuse.vertexCount=geometry.vertexCount;
    reuse.positions=maps.get()+offset;reuse.lighting=maps.get()+offset+geometry.vertexCount;
    reuse.uvs=cachedUvs+uvOffset;
    for(size_t vertex=0;vertex<geometry.vertexCount;++vertex)new(cachedUvs+uvOffset+vertex) Vector2(geometry.vertices[vertex].uv);
    uvOffset+=geometry.vertexCount;
    buildReuseMap(geometry,false,maps.get()+offset,table.get(),buckets);
    buildReuseMap(geometry,true,maps.get()+offset+geometry.vertexCount,table.get(),buckets);
    if(attributes&&geometry.vertexCount&&geometry.vertexCount<=UINT16_MAX){
      uint32_t positionCount=0,normalColorCount=0;
      auto* positions=maps.get()+offset;auto* lighting=positions+geometry.vertexCount;
      for(uint32_t vertex=0;vertex<geometry.vertexCount;++vertex){positionCount+=positions[vertex]==vertex;normalColorCount+=lighting[vertex]==vertex;}
      const size_t inputBytes=size_t(positionCount)*sizeof(Vector3)+size_t(normalColorCount)*sizeof(detail::StaticNormalColor)+geometry.vertexCount*sizeof(uint16_t);
      // Fragmentation can leave enough total HP RAM but no50KB block. Keep
      // this geometry transactional across three smaller INTERNAL allocations.
      Storage::OwnedStaticAttributes pending;pending.cacheState=cache.get();
      struct Component {void** pointer;size_t bytes;detail::ModelCacheKind kind;};
      Component components[]={
        {&pending.positions,size_t(positionCount)*sizeof(Vector3),detail::ModelCacheKind::Positions},
        {&pending.normalColors,size_t(normalColorCount)*sizeof(detail::StaticNormalColor),detail::ModelCacheKind::NormalColors},
        {&pending.rigidJoints,geometry.vertexCount*sizeof(uint16_t),detail::ModelCacheKind::RigidJoints}};
      // Fixed three-item descending order, no setup heap allocation for sorting.
      // Pending ownership still releases every earlier block on any failure.
      if(components[0].bytes<components[1].bytes)std::swap(components[0],components[1]);
      if(components[1].bytes<components[2].bytes)std::swap(components[1],components[2]);
      if(components[0].bytes<components[1].bytes)std::swap(components[0],components[1]);
      for(const auto& component:components){
        *component.pointer=cache?cache->acquire(this,component.kind,i,component.bytes,MemoryPreference::InternalOnly,128):detail::allocate(component.bytes,MemoryPreference::InternalOnly);
        if(!*component.pointer)break;
      }
      if(pending.positions&&pending.normalColors&&pending.rigidJoints){
        auto* cachedPositions=static_cast<Vector3*>(pending.positions);
        auto* cachedNormalColors=static_cast<detail::StaticNormalColor*>(pending.normalColors);
        auto* cachedJoints=static_cast<uint16_t*>(pending.rigidJoints);
        attributes[i].cacheState=cache.get();
        attributes[i].positions=pending.positions;pending.positions=nullptr;
        attributes[i].normalColors=pending.normalColors;pending.normalColors=nullptr;
        attributes[i].rigidJoints=pending.rigidJoints;pending.rigidJoints=nullptr;
        uint32_t pi=0,ni=0;
        bool constantColor=true,boundedNormals=true;uint32_t rigidCount=0;float maximumNormal=0;
        const uint16_t vertexColor=geometry.vertices[0].color;uint16_t maximumJoint=0;
        for(uint32_t vertex=0;vertex<geometry.vertexCount;++vertex){
          const auto& original=geometry.vertices[vertex];
          if(positions[vertex]==vertex)new(cachedPositions+pi++) Vector3(original.position);
          if(lighting[vertex]==vertex)new(cachedNormalColors+ni++) detail::StaticNormalColor{original.normal,original.color,0};
          uint16_t joint=UINT16_MAX;
          if(geometry.skinVertices){const auto& skin=geometry.skinVertices[vertex];
            if(skin.weights[0]==1.0f&&skin.weights[1]==0.0f&&skin.weights[2]==0.0f&&skin.weights[3]==0.0f&&skin.joints[0]!=UINT16_MAX)joint=skin.joints[0];
          }
          new(cachedJoints+vertex) uint16_t(joint);
          if(original.color!=vertexColor)constantColor=false;
          const auto& n=original.normal;
          if(!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z)||std::fabs(n.x)>1e12f||std::fabs(n.y)>1e12f||std::fabs(n.z)>1e12f)boundedNormals=false;
          else maximumNormal=std::max(maximumNormal,std::max(std::fabs(n.x),std::max(std::fabs(n.y),std::fabs(n.z))));
          if(joint!=UINT16_MAX){++rigidCount;maximumJoint=std::max(maximumJoint,joint);}
        }
        packAttributeReuseMap(positions,geometry.vertexCount);packAttributeReuseMap(lighting,geometry.vertexCount);
        reuse.sourceMask=UINT16_MAX;reuse.originalPositions=cachedPositions;reuse.originalNormalColors=cachedNormalColors;reuse.rigidJoints=cachedJoints;
        reuse.cachedPositions=positionCount;reuse.cachedNormalColors=normalColorCount;reuse.cachedInputBytes=uint32_t(inputBytes);
        reuse.constantVertexColor=vertexColor;reuse.maximumRigidJoint=maximumJoint;reuse.maximumBasicNormal=maximumNormal;
        reuse.constantColorValid=constantColor;reuse.boundedRigidNormals=boundedNormals&&rigidCount!=0;
        attributeBytes+=inputBytes;++attributeMeshes;
      }
    }
    offset+=geometry.vertexCount*2;
  }
  // Publish all maps together after every setup allocation has succeeded.
  clearStaticGeometryOptimization();storage_->staticMaps=maps.release();storage_->staticCacheState=cache;
  storage_->staticReuse=std::move(metadata);
  storage_->staticAttributes=std::move(attributes);
  storage_->staticAttributeBytes=attributeBytes;storage_->staticAttributeMeshes=attributeMeshes;
  storage_->staticMapBytes=total*16+storage_->geometries.size()*sizeof(detail::StaticVertexReuse)+attributeBytes;
  transaction.commit(false);
  for(size_t i=0;i<storage_->geometries.size();++i)storage_->geometries[i].staticReuse_=&storage_->staticReuse[i];
  status_=Status::Ok;error_="ok";return status_;
}
Status Model::retainOptimizations(ModelCacheWorkspace& workspace){
  if(!storage_){status_=Status::NotInitialized;error_="model is not initialized";return status_;}
  if(workspace.state_&&workspace.state_->count){status_=Status::Busy;error_="adoption needs an empty cache workspace";return status_;}
  if(storage_->staticCacheState||storage_->textureCacheState){status_=Status::Busy;error_="cache already belongs to a workspace";return status_;}
  // A failed rebuild can leave a valid older cache while public geometry or
  // texture descriptors refer to a different source. Never infer its capacity
  // from those changed descriptors or adopt it as the changed source layout.
  if(storage_->staticMaps){
    if(!storage_->staticReuse){status_=Status::InvalidData;error_="static cache has no committed source snapshot";return status_;}
    for(size_t i=0;i<storage_->geometries.size();++i){const auto& geometry=storage_->geometries[i];const auto& old=storage_->staticReuse[i];
      if(geometry.vertices!=old.vertices||geometry.skinVertices!=old.skinVertices||geometry.vertexCount!=old.vertexCount){
        status_=Status::InvalidData;error_="restore or clear changed static cache sources before adoption";return status_;
      }
    }
  }
  for(const auto& owned:storage_->textures)if(owned.tiledPixels||owned.compactBlocks){const auto& t=owned.texture;const auto& old=owned.tiles;
    if(old.sourcePixels!=t.pixels||old.width!=t.width||old.height!=t.height||old.stride!=t.stride){
      status_=Status::InvalidData;error_="restore or clear changed texture cache sources before adoption";return status_;
    }
  }
  auto cache=detail::ModelCacheState::create();
  if(!cache){status_=Status::NoMemory;error_="not enough memory for cache workspace";return status_;}
  const auto add=[&](detail::ModelCacheKind kind,size_t index,void* pointer,size_t bytes,MemoryPreference preference,size_t alignment){
    return cache->addBorrowed(this,kind,index,pointer,bytes,preference,alignment);
  };
  bool valid=true;
  if(storage_->staticMaps){
    size_t vertices=0;for(size_t i=0;i<storage_->geometries.size();++i)vertices+=storage_->staticReuse[i].vertexCount;
    valid=add(detail::ModelCacheKind::Maps,0,storage_->staticMaps,vertices*16,MemoryPreference::InternalFirst,128);
  }
  if(valid&&storage_->staticReuse&&storage_->staticAttributes)for(size_t i=0;i<storage_->geometries.size();++i){
    const auto& reuse=storage_->staticReuse[i];auto& owned=storage_->staticAttributes[i];
    valid=add(detail::ModelCacheKind::Positions,i,owned.positions,size_t(reuse.cachedPositions)*sizeof(Vector3),MemoryPreference::InternalOnly,128)&&
      add(detail::ModelCacheKind::NormalColors,i,owned.normalColors,size_t(reuse.cachedNormalColors)*sizeof(detail::StaticNormalColor),MemoryPreference::InternalOnly,128)&&
      add(detail::ModelCacheKind::RigidJoints,i,owned.rigidJoints,reuse.vertexCount*sizeof(uint16_t),MemoryPreference::InternalOnly,128);
    if(!valid)break;
  }
  if(valid)for(size_t i=0;i<storage_->textures.size();++i){auto& texture=storage_->textures[i];
    if(texture.compactBlocks&&!add(detail::ModelCacheKind::TextureBlocks,i,texture.compactBlocks.get(),size_t(texture.tiles.columns)*((size_t(texture.tiles.height)+7)/8)*sizeof(uint32_t),MemoryPreference::InternalFirst,64)){valid=false;break;}
    if(texture.tiledPixels&&!add(detail::ModelCacheKind::TexturePixels,i,texture.tiledPixels.get(),texture.tiles.payloadBytes,MemoryPreference::ExternalFirst,64)){valid=false;break;}
  }
  if(!valid||!cache->count){
    // Borrowed records never own the current cache until every record fits.
    cache->count=cache->active=0;cache->owner=nullptr;
    status_=valid?Status::InvalidArgument:Status::CapacityExceeded;error_="cache workspace adoption cannot represent these caches";return status_;
  }
  cache->ownBorrowed();
  if(storage_->staticMaps){storage_->staticCacheState=cache;cache->staticFrozen=true;}
  if(storage_->staticAttributes)for(size_t i=0;i<storage_->geometries.size();++i)storage_->staticAttributes[i].cacheState=cache.get();
  bool textures=false;
  for(auto& texture:storage_->textures){
    if(texture.compactBlocks){texture.compactBlocks.get_deleter().state=cache.get();textures=true;}
    if(texture.tiledPixels){texture.tiledPixels.get_deleter().state=cache.get();textures=true;}
  }
  if(textures){storage_->textureCacheState=cache;cache->textureFrozen=true;}
  workspace.state_=cache;status_=Status::Ok;error_="ok";return status_;
}

void Model::clear(){for(auto* child:root_.children)if(child&&child->parent==&root_)child->parent=nullptr;root_.children.clear();storage_.reset();status_=Status::NotInitialized;error_="model is not initialized";}
Status Model::load(const void* data,size_t length,const ModelLimits& limits){ModelView view;const auto result=view.parse(data,length,limits);if(result!=Status::Ok){status_=result;error_=view.error();return result;}return load(view);}
Status Model::load(const ModelView& view){
  if(!view.valid()){status_=view.status();error_=view.error();return status_;}
  // The public API never emits an exception when toolchain exceptions are on.
  // ESP-IDF's no-exceptions STL follows its configured allocation-failure policy.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {return loadOwned(view);}catch(const std::bad_alloc&){status_=Status::NoMemory;error_="not enough memory for the model scene";return status_;}catch(...){status_=Status::InvalidData;error_="model scene construction failed";return status_;}
#else
  return loadOwned(view);
#endif
}
Status Model::loadOwned(const ModelView& view){
  std::unique_ptr<Storage> next(new(std::nothrow) Storage());if(!next){status_=Status::NoMemory;error_="not enough memory for model storage";return status_;}
  next->vertices.resize(view.vertexCount());for(uint32_t i=0;i<view.vertexCount();++i)next->vertices[i]=view.vertex(i);
  next->skinVertices.resize(view.skinVertexCount());for(uint32_t i=0;i<view.skinVertexCount();++i)next->skinVertices[i]=view.skinVertex(i);
  next->indices.resize(view.indexCount());for(uint32_t i=0;i<view.indexCount();++i)next->indices[i]=view.index(i);
  next->geometries.resize(view.primitiveCount());for(uint32_t i=0;i<view.primitiveCount();++i){const auto p=view.primitive(i);auto& g=next->geometries[i];g.vertices=next->vertices.data()+p.vertexStart;g.vertexCount=p.vertexCount;g.indices=next->indices.data()+p.indexStart;g.indexCount=p.indexCount;g.skinVertices=p.skinStart==0xffffffffu?nullptr:next->skinVertices.data()+p.skinStart;g.computeBoundingSphere();}
  next->textures.resize(view.textureCount());for(uint32_t i=0;i<view.textureCount();++i){const auto t=view.texture(i);auto& target=next->textures[i];const size_t count=size_t(t.width)*t.height;target.pixels.resize(count);if(t.alphaOffset)target.alpha.resize(count);for(size_t j=0;j<count;++j){target.pixels[j]=view.texturePixel(t,uint32_t(j));if(t.alphaOffset)target.alpha[j]=view.textureAlpha(t,uint32_t(j));}target.texture=Texture(target.pixels.data(),t.width,t.height,t.width,target.alpha.empty()?nullptr:target.alpha.data());target.texture.wrapS=Wrap(t.samplerFlags&3u);target.texture.wrapT=Wrap((t.samplerFlags>>2)&3u);}
  next->materials.resize(size_t(view.materialCount())+1);for(uint32_t i=0;i<view.materialCount();++i){const auto m=view.material(i);auto& target=next->materials[i];target.color=color565(m.baseColor);target.opacity=(m.flags&6u)?m.baseColor[3]:1;target.side=(m.flags&1u)?Side::Double:Side::Front;target.transparent=(m.flags&4u)!=0;target.depthWrite=!target.transparent;target.alphaTest=(m.flags&2u)?m.alphaCutoff:0;if(m.texture>=0)target.map=&next->textures[size_t(m.texture)].texture;}
  next->nodes.reserve(view.nodeCount());next->nodeNames.resize(view.nodeCount());
  for(uint32_t i=0;i<view.nodeCount();++i){const auto source=view.node(i);std::unique_ptr<Object3D> object(new(std::nothrow) Group());if(!object){status_=Status::NoMemory;error_="not enough memory for model nodes";return status_;}next->nodeNames[i]=source.name;object->name=next->nodeNames[i].c_str();object->position=source.position;object->scale=source.scale;object->setQuaternion(source.quaternion);next->nodes.push_back(std::move(object));}
  for(uint32_t i=0;i<view.nodeCount();++i){const auto source=view.node(i);if(source.parent>=0)next->nodes[size_t(source.parent)]->add(*next->nodes[i]);}
  next->skeletons.reserve(view.skinCount());
  for(uint32_t i=0;i<view.skinCount();++i){const auto source=view.skin(i);std::unique_ptr<Skeleton> skeleton(new(std::nothrow) Skeleton());if(!skeleton){status_=Status::NoMemory;error_="not enough memory for skeletons";return status_;}skeleton->bones.resize(source.jointCount);skeleton->boneInverses.resize(source.jointCount);skeleton->boneMatrices.resize(source.jointCount);for(uint32_t j=0;j<source.jointCount;++j){skeleton->bones[j]=next->nodes[view.joint(source,j)].get();skeleton->boneInverses[j]=view.inverseBindMatrix(source,j);}next->skeletons.push_back(std::move(skeleton));}
  for(uint32_t i=0;i<view.nodeCount();++i){const auto source=view.node(i);if(source.mesh<0)continue;const auto mesh=view.mesh(uint32_t(source.mesh));for(uint32_t p=0;p<mesh.primitiveCount;++p){const uint32_t primitiveIndex=mesh.firstPrimitive+p;const auto primitive=view.primitive(primitiveIndex);auto& geometry=next->geometries[primitiveIndex];auto& material=next->materials[primitive.material<0?size_t(view.materialCount()):size_t(primitive.material)];std::unique_ptr<Mesh> object;if(source.skin>=0){auto* skinned=new(std::nothrow) SkinnedMesh(geometry,material);if(skinned)skinned->bind(*next->skeletons[size_t(source.skin)]);object.reset(skinned);}else object.reset(new(std::nothrow) Mesh(geometry,material));if(!object){status_=Status::NoMemory;error_="not enough memory for mesh instances";return status_;}next->nodes[i]->add(*object);next->meshInstances.push_back(std::move(object));}}
  next->trackData.resize(view.channelCount());
  next->tracks.resize(view.channelCount());
  for(uint32_t i=0;i<view.channelCount();++i){
    const auto source=view.channel(i);
    auto& data=next->trackData[i];
    const size_t components=source.property==TrackProperty::Quaternion?4:3;
    if(!data.times.allocate(source.keyCount)||!data.values.allocate(size_t(source.keyCount)*components)){
      status_=Status::NoMemory;error_="not enough memory for animation keys";return status_;
    }
    float* times=data.times.data();
    float* values=data.values.data();
    for(uint32_t j=0;j<source.keyCount;++j){
      times[j]=view.keyTime(source,j);
      for(uint32_t c=0;c<components;++c)values[size_t(j)*components+c]=view.keyValue(source,j,c);
      if(components==4){
        Quaternion q(values[j*4],values[j*4+1],values[j*4+2],values[j*4+3]);q.normalize();
        values[j*4]=q.x;values[j*4+1]=q.y;values[j*4+2]=q.z;values[j*4+3]=q.w;
      }
    }
    auto& track=next->tracks[i];
    track.target=next->nodes[source.node].get();track.property=source.property;
    track.interpolation=source.interpolation;track.times=times;track.values=values;
    track.keyCount=source.keyCount;
  }
  next->clipNames.resize(view.animationCount());next->clips.resize(view.animationCount());for(uint32_t i=0;i<view.animationCount();++i){const auto source=view.animation(i);next->clipNames[i]=source.name;float duration=0;for(uint32_t j=0;j<source.channelCount;++j){const auto& track=next->tracks[source.firstChannel+j];duration=std::max(duration,track.times[track.keyCount-1]);}next->clips[i]=AnimationClip(next->clipNames[i].c_str(),duration,next->tracks.data()+source.firstChannel,source.channelCount);}
  // Reserve before replacing the live model, so a failed setup does not leave
  // pointers to a partially built hierarchy in the application's scene.
  root_.children.reserve(view.rootCount());clear();storage_=std::move(next);
  for(uint32_t i=0;i<view.rootCount();++i)root_.add(*storage_->nodes[view.root(i)]);
  // World matrices are updated by the renderer (or explicitly by the caller).
  // Avoid recursing through imported hierarchies on the loading task's stack.
  status_=Status::Ok;error_="ok";return status_;
}
size_t Model::nodeCount()const{return storage_?storage_->nodes.size():0;}
Object3D* Model::node(size_t i){return storage_&&i<storage_->nodes.size()?storage_->nodes[i].get():nullptr;}
const Object3D* Model::node(size_t i)const{return storage_&&i<storage_->nodes.size()?storage_->nodes[i].get():nullptr;}
size_t Model::animationCount()const{return storage_?storage_->clips.size():0;}
const AnimationClip* Model::animation(size_t i)const{return storage_&&i<storage_->clips.size()?&storage_->clips[i]:nullptr;}
const AnimationClip& Model::clip(size_t i)const{static const AnimationClip empty;const auto* value=animation(i);return value?*value:empty;}
const std::vector<AnimationClip>& Model::animations()const{static const std::vector<AnimationClip> empty;return storage_?storage_->clips:empty;}
} // namespace nice_gfx_3d
