#pragma once
#include "Math3D.h"
#include <cstdint>

namespace nice_gfx_3d {
// Author-friendly 24-bit RGB input; converts once at the material boundary.
// MeshBasicMaterial(Color(0xff8800)) keeps RGB565 out of ordinary author code.
struct Color {
  float r=1,g=1,b=1;
  Color()=default;
  explicit Color(uint32_t hex){setHex(hex);}
  Color(float red,float green,float blue):r(red),g(green),b(blue){}
  Color&setHex(uint32_t hex){r=float((hex>>16)&255)/255;g=float((hex>>8)&255)/255;b=float(hex&255)/255;return *this;}
  Color&setRGB(float red,float green,float blue){r=red;g=green;b=blue;return *this;}
  static Color fromRGB565(uint16_t value){return Color(float(value>>11)/31,float((value>>5)&63)/63,float(value&31)/31);}
  uint16_t toRGB565()const{return uint16_t((uint16_t(clamp(r,0,1)*31+.5f)<<11)|(uint16_t(clamp(g,0,1)*63+.5f)<<5)|uint16_t(clamp(b,0,1)*31+.5f));}
  operator uint16_t()const{return toRGB565();}
};
}
