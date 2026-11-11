#pragma once
#include "Math3D.h"
#include <cstdint>
#include <cstring>
#include <limits>

#define NICE_GFX_3D_HAS_PALETTE_TEXTURE_TILES 1
namespace nice_gfx_3d {
enum class Side:uint8_t {Front,Back,Double};
enum class MaterialType:uint8_t {Basic,Lambert};
enum class Filter:uint8_t {Nearest};
enum class Wrap:uint8_t {ClampToEdge,Repeat,MirroredRepeat};
namespace detail {
struct TextureTiles {
  const uint16_t* sourcePixels=nullptr;
  const uint16_t* pixels=nullptr;
  uint32_t width=0,height=0,stride=0,columns=0;
  // Bit31:solid RGB565. Bit30:4bpp palette. Otherwise raw RGB565.
  // Non-solid low30bits hold a64-byte aligned payload offset divided by64.
  const uint32_t* compactBlocks=nullptr;
  size_t payloadBytes=0;
  uint32_t solidTileCount=0,paletteTileCount=0;
};
}

// Borrowed native-endian RGB565 pixels, optional A8 plane. Both use stride pixels.
// Data must outlive every material/render that references this texture.
struct Texture {
  const uint16_t* pixels=nullptr;
  const uint8_t* alpha=nullptr;
  uint32_t width=0,height=0,stride=0;
  Filter filter=Filter::Nearest;
  Wrap wrapS=Wrap::ClampToEdge,wrapT=Wrap::ClampToEdge;
  bool flipY=false;
  Texture()=default;
  Texture(const uint16_t* data,uint32_t w,uint32_t h,uint32_t rowStride=0,const uint8_t* a=nullptr):pixels(data),alpha(a),width(w),height(h),stride(rowStride?rowStride:w){}
  // Copies remain ordinary row-major borrowed views. Only the renderer's
  // synchronous batch may borrow Model-owned optimization metadata.
  Texture(const Texture& other){*this=other;}
  Texture& operator=(const Texture& other){
    if(this!=&other){
      pixels=other.pixels;alpha=other.alpha;width=other.width;height=other.height;stride=other.stride;
      filter=other.filter;wrapS=other.wrapS;wrapT=other.wrapT;flipY=other.flipY;tiles_=nullptr;
    }
    return *this;
  }
  bool valid()const{return pixels&&width&&height&&stride>=width&&size_t(height)<=SIZE_MAX/stride;}
  static float wrap(float v,Wrap mode){
    // Positive binary32 values below one are ordered by their representation.
    // Integer checks also leave the FP exception flags untouched for NaN input.
    static_assert(sizeof(float)==sizeof(uint32_t)&&std::numeric_limits<float>::is_iec559,
        "Textures require IEEE-754 binary32 floats");
    uint32_t representation;
    std::memcpy(&representation,&v,sizeof(representation));
    if(representation>0&&representation<0x3f800000u)return v;
    const uint32_t magnitude=representation&0x7fffffffu;
    if(!magnitude||magnitude>=0x7f800000u)return 0.0f;
    if(mode==Wrap::ClampToEdge)return clamp(v,0,1);
    const float tile=std::floor(v),f=v-tile;
    return mode==Wrap::MirroredRepeat&&std::fmod(std::fabs(tile),2.0f)>=1?1-f:f;
  }
  size_t texel(float u,float v)const{u=wrap(u,wrapS);v=wrap(v,wrapT);if(flipY)v=1-v;const uint32_t x=std::min(width-1,uint32_t(u*float(width))),y=std::min(height-1,uint32_t(v*float(height)));return size_t(y)*stride+x;}
  uint16_t sample(float u,float v)const{return valid()?pixels[texel(u,v)]:uint16_t(0xffff);}
  uint8_t sampleAlpha(float u,float v)const{return valid()&&alpha?alpha[texel(u,v)]:uint8_t(255);}
 private:
  friend class Model;
  friend class Renderer;
  const detail::TextureTiles* tiles_=nullptr;
};
struct Material {
  explicit Material(MaterialType kind=MaterialType::Basic,uint16_t rgb565=0xffff):color(rgb565),type(kind){}
  virtual ~Material()=default;
  uint16_t color=0xffff;
  MaterialType type=MaterialType::Basic;
  const Texture* map=nullptr;
  Side side=Side::Front;
  float opacity=1.0f,alphaTest=0.0f;
  bool depthTest=true,depthWrite=true,visible=true,transparent=false;
};
struct MeshBasicMaterial:Material {explicit MeshBasicMaterial(uint16_t rgb565=0xffff):Material(MaterialType::Basic,rgb565){}};
struct MeshLambertMaterial:Material {explicit MeshLambertMaterial(uint16_t rgb565=0xffff):Material(MaterialType::Lambert,rgb565){}};
} // namespace nice_gfx_3d
