#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// 0 keeps the original pair loop and ROM depth memset. 1 uses unrolled words.
// 2 copies an internal-stack 1 KiB seed. 3 uses ESP-IDF PPA Fill with CPU fallback.
#ifndef NICE_GFX_3D_CLEAR_METHOD
#define NICE_GFX_3D_CLEAR_METHOD 3
#endif

namespace nice_gfx_3d { namespace detail {
// PPA's RGB565 output still takes an 8-bit ARGB input colour. Bit replication
// preserves every RGB565 value when the hardware quantizes it back to 5/6/5.
constexpr uint32_t ppaFillArgb8888FromRgb565(uint16_t color) {
  const uint32_t red = color >> 11;
  const uint32_t green = (color >> 5) & 63;
  const uint32_t blue = color & 31;
  const uint32_t red8 = (red << 3) | (red >> 2);
  const uint32_t green8 = (green << 2) | (green >> 4);
  const uint32_t blue8 = (blue << 3) | (blue >> 2);
  return 0xff000000u | (red8 << 16) | (green8 << 8) | blue8;
}
inline void fill565Original(uint16_t* pixels,size_t count,uint16_t color){
  const uint32_t pair=uint32_t(color)|(uint32_t(color)<<16);
  while(count>=2){std::memcpy(pixels,&pair,4);pixels+=2;count-=2;}
  if(count)*pixels=color;
}
inline void fill565Wide(uint16_t* pixels,size_t count,uint16_t color){
  if(!count)return;
  if(reinterpret_cast<uintptr_t>(pixels)&3){*pixels++=color;--count;}
#if defined(__GNUC__) || defined(__clang__)
  typedef uint32_t AliasWord __attribute__((__may_alias__));
  auto* words=reinterpret_cast<AliasWord*>(pixels);
  const uint32_t pair=uint32_t(color)|(uint32_t(color)<<16);
  while(count>=16){
    words[0]=pair;words[1]=pair;words[2]=pair;words[3]=pair;
    words[4]=pair;words[5]=pair;words[6]=pair;words[7]=pair;
    words+=8;count-=16;
  }
  while(count>=2){*words++=pair;count-=2;}
  pixels=reinterpret_cast<uint16_t*>(words);
  if(count)*pixels=color;
#else
  std::fill_n(pixels,count,color);
#endif
}
inline void fill565Seed(uint16_t* pixels,size_t count,uint16_t color){
  if(count<512){fill565Wide(pixels,count,color);return;}
  alignas(16) uint32_t pattern[256];
  const uint32_t pair=uint32_t(color)|(uint32_t(color)<<16);
  for(auto& word:pattern)word=pair;
  while(count>=512){std::memcpy(pixels,pattern,sizeof(pattern));pixels+=512;count-=512;}
  fill565Wide(pixels,count,color);
}
inline void fill565Cpu(uint16_t* pixels,size_t count,uint16_t color){
#if NICE_GFX_3D_CLEAR_METHOD == 0
  fill565Original(pixels,count,color);
#elif NICE_GFX_3D_CLEAR_METHOD == 2
  fill565Seed(pixels,count,color);
#else
  fill565Wide(pixels,count,color);
#endif
}
} }
