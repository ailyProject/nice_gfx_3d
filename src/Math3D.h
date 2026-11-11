#pragma once
// A distinct basename avoids shadowing the C library's <math.h> on Windows.
#include <algorithm>
#include <cmath>
#include <cstddef>

// Keep small vertex operations in the caller's optimization domain. Arduino
// sketches may emit size-optimized weak COMDAT copies of public inline methods;
// an O3 Renderer TU must not call the linker's selected Os copy per vertex.
#ifndef NICE_GFX_3D_HOT_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define NICE_GFX_3D_HOT_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define NICE_GFX_3D_HOT_INLINE __forceinline
#else
#define NICE_GFX_3D_HOT_INLINE inline
#endif
#endif

namespace nice_gfx_3d {
constexpr float Pi = 3.14159265358979323846f;
inline float clamp(float value,float low,float high){return std::max(low,std::min(value,high));}
inline float degToRad(float value){return value*(Pi/180.0f);}

struct Matrix4;
struct Quaternion;
struct Vector2 {
  float x=0,y=0;
  constexpr Vector2()=default;
  constexpr Vector2(float x_,float y_):x(x_),y(y_){}
  Vector2& set(float x_,float y_){x=x_;y=y_;return *this;}
  Vector2& add(const Vector2& v){x+=v.x;y+=v.y;return *this;}
  Vector2& sub(const Vector2& v){x-=v.x;y-=v.y;return *this;}
  Vector2& multiplyScalar(float s){x*=s;y*=s;return *this;}
  Vector2& lerp(const Vector2& v,float t){x+=(v.x-x)*t;y+=(v.y-y)*t;return *this;}
};
struct Vector3 {
  float x=0,y=0,z=0;
  constexpr Vector3()=default;
  constexpr Vector3(float x_,float y_,float z_):x(x_),y(y_),z(z_){}
  NICE_GFX_3D_HOT_INLINE Vector3& set(float x_,float y_,float z_){x=x_;y=y_;z=z_;return *this;}
  Vector3& add(const Vector3& v){x+=v.x;y+=v.y;z+=v.z;return *this;}
  Vector3& sub(const Vector3& v){x-=v.x;y-=v.y;z-=v.z;return *this;}
  NICE_GFX_3D_HOT_INLINE Vector3& addScaledVector(const Vector3& v,float s){x+=v.x*s;y+=v.y*s;z+=v.z*s;return *this;}
  NICE_GFX_3D_HOT_INLINE Vector3& multiplyScalar(float s){x*=s;y*=s;z*=s;return *this;}
  Vector3& multiply(const Vector3& v){x*=v.x;y*=v.y;z*=v.z;return *this;}
  NICE_GFX_3D_HOT_INLINE float dot(const Vector3& v)const{return x*v.x+y*v.y+z*v.z;}
  NICE_GFX_3D_HOT_INLINE float lengthSq()const{return dot(*this);}
  NICE_GFX_3D_HOT_INLINE float length()const{return std::sqrt(lengthSq());}
  NICE_GFX_3D_HOT_INLINE Vector3& normalize(){const float n=length();if(n>0&&std::isfinite(n))multiplyScalar(1.0f/n);return *this;}
  Vector3& crossVectors(const Vector3& a,const Vector3& b){return set(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
  Vector3& cross(const Vector3& v){return crossVectors(*this,v);}
  Vector3& lerp(const Vector3& v,float t){x+=(v.x-x)*t;y+=(v.y-y)*t;z+=(v.z-z)*t;return *this;}
  NICE_GFX_3D_HOT_INLINE Vector3& applyMatrix4(const Matrix4& m);
  Vector3& transformDirection(const Matrix4& m);
  Vector3& applyQuaternion(const Quaternion& q);
  NICE_GFX_3D_HOT_INLINE bool isFinite()const{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}
};
inline Vector3 operator+(Vector3 a,const Vector3& b){return a.add(b);}
inline Vector3 operator-(Vector3 a,const Vector3& b){return a.sub(b);}
inline Vector3 operator*(Vector3 a,float s){return a.multiplyScalar(s);}
inline Vector3 operator*(float s,Vector3 a){return a.multiplyScalar(s);}

struct Quaternion {
  float x=0,y=0,z=0,w=1;
  constexpr Quaternion()=default;
  constexpr Quaternion(float x_,float y_,float z_,float w_):x(x_),y(y_),z(z_),w(w_){}
  Quaternion& set(float x_,float y_,float z_,float w_){x=x_;y=y_;z=z_;w=w_;return *this;}
  float dot(const Quaternion& q)const{return x*q.x+y*q.y+z*q.z+w*q.w;}
  Quaternion& identity(){return set(0,0,0,1);}
  Quaternion& normalize(){const float n=std::sqrt(dot(*this));if(!(n>0)||!std::isfinite(n))return identity();x/=n;y/=n;z/=n;w/=n;return *this;}
  Quaternion& conjugate(){x=-x;y=-y;z=-z;return *this;}
  Quaternion& invert(){return conjugate().normalize();}
  Quaternion& multiplyQuaternions(const Quaternion& a,const Quaternion& b){return set(a.x*b.w+a.w*b.x+a.y*b.z-a.z*b.y,a.y*b.w+a.w*b.y+a.z*b.x-a.x*b.z,a.z*b.w+a.w*b.z+a.x*b.y-a.y*b.x,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);}
  Quaternion& multiply(const Quaternion& q){return multiplyQuaternions(*this,q);}
  // Euler XYZ in radians. Cameras look down local -Z.
  Quaternion& setFromEuler(const Vector3& e){const float c1=std::cos(e.x*.5f),c2=std::cos(e.y*.5f),c3=std::cos(e.z*.5f),s1=std::sin(e.x*.5f),s2=std::sin(e.y*.5f),s3=std::sin(e.z*.5f);return set(s1*c2*c3+c1*s2*s3,c1*s2*c3-s1*c2*s3,c1*c2*s3+s1*s2*c3,c1*c2*c3-s1*s2*s3);}
  Quaternion& setFromAxisAngle(Vector3 axis,float angle){axis.normalize();if(axis.lengthSq()==0)return identity();const float s=std::sin(angle*.5f);return set(axis.x*s,axis.y*s,axis.z*s,std::cos(angle*.5f));}
  Quaternion& setFromRotationMatrix(const Matrix4& m);
  Quaternion& slerp(const Quaternion& target,float t){
    Quaternion b=target;float d=dot(b);if(d<0){b.set(-b.x,-b.y,-b.z,-b.w);d=-d;}
    d=clamp(d,-1,1);if(d>.9995f){x+=(b.x-x)*t;y+=(b.y-y)*t;z+=(b.z-z)*t;w+=(b.w-w)*t;return normalize();}
    const float angle=std::acos(d),den=std::sin(angle),a=std::sin((1-t)*angle)/den,s=std::sin(t*angle)/den;
    return set(x*a+b.x*s,y*a+b.y*s,z*a+b.z*s,w*a+b.w*s).normalize();
  }
};

struct Matrix4 {
  // Column-major, column vectors: clip = projection * view * world * position.
  float elements[16];
  Matrix4(){identity();}
  Matrix4& identity(){for(size_t i=0;i<16;++i)elements[i]=(i%5==0)?1.0f:0.0f;return *this;}
  Matrix4& fromArray(const float* data){if(data)for(size_t i=0;i<16;++i)elements[i]=data[i];return *this;}
  Matrix4& multiplyMatrices(const Matrix4& a,const Matrix4& b){float r[16];for(size_t col=0;col<4;++col)for(size_t row=0;row<4;++row){float v=0;for(size_t k=0;k<4;++k)v+=a.elements[k*4+row]*b.elements[col*4+k];r[col*4+row]=v;}return fromArray(r);}
  Matrix4& multiply(const Matrix4& m){return multiplyMatrices(*this,m);}
  Matrix4& premultiply(const Matrix4& m){return multiplyMatrices(m,*this);}
  Matrix4& makeTranslation(float x,float y,float z){identity();elements[12]=x;elements[13]=y;elements[14]=z;return *this;}
  Matrix4& makeScale(float x,float y,float z){identity();elements[0]=x;elements[5]=y;elements[10]=z;return *this;}
  Matrix4& compose(const Vector3& p,const Quaternion& input,const Vector3& s){
    Quaternion q=input;q.normalize();const float x=q.x,y=q.y,z=q.z,w=q.w,x2=x+x,y2=y+y,z2=z+z,xx=x*x2,xy=x*y2,xz=x*z2,yy=y*y2,yz=y*z2,zz=z*z2,wx=w*x2,wy=w*y2,wz=w*z2;
    elements[0]=(1-(yy+zz))*s.x;elements[1]=(xy+wz)*s.x;elements[2]=(xz-wy)*s.x;elements[3]=0;
    elements[4]=(xy-wz)*s.y;elements[5]=(1-(xx+zz))*s.y;elements[6]=(yz+wx)*s.y;elements[7]=0;
    elements[8]=(xz+wy)*s.z;elements[9]=(yz-wx)*s.z;elements[10]=(1-(xx+yy))*s.z;elements[11]=0;
    elements[12]=p.x;elements[13]=p.y;elements[14]=p.z;elements[15]=1;return *this;
  }
  bool tryInvert(){
    // Pivoted elimination also handles non-affine projection matrices. No heap.
    float a[4][8];for(size_t r=0;r<4;++r)for(size_t c=0;c<8;++c)a[r][c]=c<4?elements[c*4+r]:(c-4==r?1.0f:0.0f);
    for(size_t c=0;c<4;++c){size_t pivot=c;for(size_t r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[pivot][c]))pivot=r;
      if(!std::isfinite(a[pivot][c])||std::fabs(a[pivot][c])<1e-20f)return false;
      if(pivot!=c)for(size_t k=0;k<8;++k)std::swap(a[pivot][k],a[c][k]);
      const float inv=1/a[c][c];for(size_t k=0;k<8;++k)a[c][k]*=inv;
      for(size_t r=0;r<4;++r)if(r!=c){const float factor=a[r][c];for(size_t k=0;k<8;++k)a[r][k]-=factor*a[c][k];}
    }
    for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)if(!std::isfinite(a[r][c+4]))return false;
    for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)elements[c*4+r]=a[r][c+4];return true;
  }
  // Singular input becomes identity; callers needing failure detection use tryInvert.
  Matrix4& invert(){if(!tryInvert())identity();return *this;}
  Matrix4& transpose(){for(size_t c=0;c<4;++c)for(size_t r=c+1;r<4;++r)std::swap(elements[c*4+r],elements[r*4+c]);return *this;}
  Matrix4& makePerspective(float fovDegrees,float aspect,float nearPlane,float farPlane){
    identity();if(!(fovDegrees>0&&fovDegrees<180&&aspect>0&&nearPlane>0&&farPlane>nearPlane))return *this;
    const float f=1/std::tan(degToRad(fovDegrees)*.5f);for(float& v:elements)v=0;
    elements[0]=f/aspect;elements[5]=f;elements[10]=-(farPlane+nearPlane)/(farPlane-nearPlane);elements[11]=-1;elements[14]=-2*farPlane*nearPlane/(farPlane-nearPlane);return *this;
  }
  Matrix4& makeOrthographic(float left,float right,float top,float bottom,float nearPlane,float farPlane){
    identity();if(right==left||top==bottom||farPlane==nearPlane)return *this;
    elements[0]=2/(right-left);elements[5]=2/(top-bottom);elements[10]=-2/(farPlane-nearPlane);elements[12]=-(right+left)/(right-left);elements[13]=-(top+bottom)/(top-bottom);elements[14]=-(farPlane+nearPlane)/(farPlane-nearPlane);return *this;
  }
  float getMaxScaleOnAxis()const{const auto* e=elements;return std::sqrt(std::max(e[0]*e[0]+e[1]*e[1]+e[2]*e[2],std::max(e[4]*e[4]+e[5]*e[5]+e[6]*e[6],e[8]*e[8]+e[9]*e[9]+e[10]*e[10])));}
};
NICE_GFX_3D_HOT_INLINE Vector3& Vector3::applyMatrix4(const Matrix4& m){const auto* e=m.elements;const float px=x,py=y,pz=z,w=e[3]*px+e[7]*py+e[11]*pz+e[15];const float iw=w!=0?1/w:1;return set((e[0]*px+e[4]*py+e[8]*pz+e[12])*iw,(e[1]*px+e[5]*py+e[9]*pz+e[13])*iw,(e[2]*px+e[6]*py+e[10]*pz+e[14])*iw);}
inline Vector3& Vector3::transformDirection(const Matrix4& m){const auto* e=m.elements;const float px=x,py=y,pz=z;return set(e[0]*px+e[4]*py+e[8]*pz,e[1]*px+e[5]*py+e[9]*pz,e[2]*px+e[6]*py+e[10]*pz).normalize();}
inline Vector3& Vector3::applyQuaternion(const Quaternion& q){const Vector3 u(q.x,q.y,q.z);Vector3 t;t.crossVectors(u,*this).multiplyScalar(2);Vector3 c;c.crossVectors(u,t);return addScaledVector(t,q.w).add(c);}
inline Quaternion& Quaternion::setFromRotationMatrix(const Matrix4& m){
  const auto* e=m.elements;const float m11=e[0],m22=e[5],m33=e[10],trace=m11+m22+m33;
  if(trace>0){const float s=.5f/std::sqrt(trace+1);set((e[6]-e[9])*s,(e[8]-e[2])*s,(e[1]-e[4])*s,.25f/s);}
  else if(m11>m22&&m11>m33){const float s=2*std::sqrt(1+m11-m22-m33);set(.25f*s,(e[4]+e[1])/s,(e[8]+e[2])/s,(e[6]-e[9])/s);}
  else if(m22>m33){const float s=2*std::sqrt(1+m22-m11-m33);set((e[4]+e[1])/s,.25f*s,(e[9]+e[6])/s,(e[8]-e[2])/s);}
  else {const float s=2*std::sqrt(1+m33-m11-m22);set((e[8]+e[2])/s,(e[9]+e[6])/s,.25f*s,(e[1]-e[4])/s);}return normalize();
}
} // namespace nice_gfx_3d
