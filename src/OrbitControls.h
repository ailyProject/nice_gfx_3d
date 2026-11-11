#pragma once
#include "Scene.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace nice_gfx_3d {
struct OrbitContact { int32_t id=0;float x=0,y=0; };

// Allocation-free, Y-up orbit with no inertia or panning. Coordinates are in
// display pixels; target is in world space. One viewport-width drag is 360deg.
// Call setContacts once per complete touch sample (including an empty release).
class OrbitControls {
  Camera &camera_;
  OrbitContact contacts_[2];size_t count_=0;
  float width_=1,height_=1,orthoDistance_=0;
  Vector3 savedPosition_,savedTarget_;
  float savedLeft_=0,savedRight_=0,savedTop_=0,savedBottom_=0,savedOrthoDistance_=0;
  bool saved_=false;
  static constexpr float Epsilon=0.0001f;
  bool limits(float &low,float &high)const {
    if(!std::isfinite(minDistance)||!std::isfinite(maxDistance)||minDistance<0||maxDistance<=0||minDistance>maxDistance)return false;
    low=std::max(Epsilon,minDistance);high=std::max(low,maxDistance);return true;
  }
  bool eye(Vector3 &position) {
    position=camera_.parent?camera_.getWorldPosition():camera_.position;
    return position.isFinite()&&target.isFinite();
  }
  bool place(const Vector3 &position) {
    if(!position.isFinite())return false;Vector3 local=position;
    if(camera_.parent){Matrix4 inverse=camera_.parent->matrixWorld;if(!inverse.tryInvert())return false;local.applyMatrix4(inverse);}
    if(!local.isFinite())return false;camera_.position=local;camera_.lookAt(target);return true;
  }
  bool orbit(float azimuthDelta,float polarDelta) {
    float low,high;Vector3 position;if(!limits(low,high)||!eye(position))return false;
    Vector3 offset=position-target;float radius=offset.length();
    if(!std::isfinite(radius))return false;if(radius<Epsilon){radius=low;offset.set(0,0,radius);}
    const float azimuth=std::atan2(offset.x,offset.z)+std::fmod(azimuthDelta,2.f*Pi);
    const float polar=clamp(std::acos(clamp(offset.y/radius,-1.f,1.f))+polarDelta,Epsilon,Pi-Epsilon);
    radius=clamp(radius,low,high);const float horizontal=radius*std::sin(polar);
    return place(target+Vector3(horizontal*std::sin(azimuth),radius*std::cos(polar),horizontal*std::cos(azimuth)));
  }
  bool dollyScale(float factor) {
    if(!std::isfinite(factor)||factor<=0)return false;
    float low,high;Vector3 position;if(!limits(low,high)||!eye(position))return false;
    Vector3 offset=position-target;const float radius=offset.length();if(!std::isfinite(radius))return false;
    if(camera_.type()==ObjectType::OrthographicCamera){
      auto &camera=static_cast<OrthographicCamera &>(camera_);
      if(!std::isfinite(camera.left)||!std::isfinite(camera.right)||!std::isfinite(camera.top)||!std::isfinite(camera.bottom)||camera.left>=camera.right||camera.bottom>=camera.top)return false;
      if(!(orthoDistance_>0))orthoDistance_=clamp(radius,low,high);
      const float distance=clamp(orthoDistance_*factor,low,high),scale=distance/orthoDistance_;
      const float cx=(camera.left+camera.right)*.5f,cy=(camera.top+camera.bottom)*.5f;
      const float halfWidth=(camera.right-camera.left)*.5f*scale,halfHeight=(camera.top-camera.bottom)*.5f*scale;
      if(!std::isfinite(halfWidth)||!std::isfinite(halfHeight)||halfWidth<Epsilon||halfHeight<Epsilon)return false;
      camera.left=cx-halfWidth;camera.right=cx+halfWidth;camera.top=cy+halfHeight;camera.bottom=cy-halfHeight;
      camera.updateProjectionMatrix();orthoDistance_=distance;return true;
    }
    const float distance=clamp(std::max(radius,Epsilon)*factor,low,high);
    if(radius<Epsilon)offset.set(0,0,distance);else offset.multiplyScalar(distance/radius);
    return place(target+offset);
  }
 public:
  Vector3 target;
  float minDistance=.01f,maxDistance=1000000.f,rotateSpeed=1.f,zoomSpeed=1.f;
  explicit OrbitControls(Camera &camera):camera_(camera){}
  Camera &camera(){return camera_;}
  bool interacting()const{return count_!=0;}
  void setViewport(float width,float height){if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0){cancel();return;}if(width!=width_||height!=height_){cancel();width_=width;height_=height;}}
  void cancel(){count_=0;}
  // Apply externally changed camera/target values and enforce distance limits.
  bool update(){return orbit(0,0);}
  // Programmatic controls use the same path as touch, without speed scaling.
  bool rotate(float azimuthDeltaRadians,float polarDeltaRadians){if(!std::isfinite(azimuthDeltaRadians)||!std::isfinite(polarDeltaRadians)){cancel();return false;}return orbit(azimuthDeltaRadians,polarDeltaRadians);}
  bool dolly(float distanceScale){if(!std::isfinite(distanceScale)||distanceScale<=0){cancel();return false;}return dollyScale(distanceScale);}
  void saveState(){
    cancel();Vector3 position;float low,high;if(!eye(position)||!limits(low,high))return;
    savedPosition_=camera_.position;savedTarget_=target;orthoDistance_=clamp((position-target).length(),low,high);savedOrthoDistance_=orthoDistance_;
    if(camera_.type()==ObjectType::OrthographicCamera){const auto &c=static_cast<const OrthographicCamera &>(camera_);savedLeft_=c.left;savedRight_=c.right;savedTop_=c.top;savedBottom_=c.bottom;}
    saved_=true;
  }
  void reset(){
    cancel();if(!saved_)return;target=savedTarget_;camera_.position=savedPosition_;orthoDistance_=savedOrthoDistance_;
    if(camera_.type()==ObjectType::OrthographicCamera){auto &c=static_cast<OrthographicCamera &>(camera_);c.left=savedLeft_;c.right=savedRight_;c.top=savedTop_;c.bottom=savedBottom_;c.updateProjectionMatrix();}
    camera_.lookAt(target);
  }
  void setContacts(const OrbitContact *points,size_t count){
    if(!count){cancel();return;}if(!points||count>2||!std::isfinite(rotateSpeed)||!std::isfinite(zoomSpeed)){cancel();return;}
    OrbitContact next[2];for(size_t i=0;i<count;++i){next[i]=points[i];if(!std::isfinite(next[i].x)||!std::isfinite(next[i].y)){cancel();return;}}
    if(count==2&&next[0].id==next[1].id){cancel();return;}
    // Stable IDs, rather than report order, decide whether this is one gesture.
    if(count==2&&count_==2&&next[0].id==contacts_[1].id&&next[1].id==contacts_[0].id)std::swap(next[0],next[1]);
    bool same=count==count_;for(size_t i=0;same&&i<count;++i)same=next[i].id==contacts_[i].id;
    if(same){
      if(count==1){const float dx=next[0].x-contacts_[0].x,dy=next[0].y-contacts_[0].y;if(!std::isfinite(dx)||!std::isfinite(dy)){cancel();return;}rotate(-dx/width_*2.f*Pi*rotateSpeed,-dy/height_*Pi*rotateSpeed);}
      else {const float previous=std::hypot(contacts_[1].x-contacts_[0].x,contacts_[1].y-contacts_[0].y),current=std::hypot(next[1].x-next[0].x,next[1].y-next[0].y);if(std::isfinite(previous)&&std::isfinite(current)&&previous>=Epsilon&&current>=Epsilon){const float exponent=std::log(previous/current)*zoomSpeed;if(std::isfinite(exponent))dolly(std::exp(clamp(exponent,-20.f,20.f)));}}
    }
    for(size_t i=0;i<count;++i)contacts_[i]=next[i];count_=count;
  }
};
} // namespace nice_gfx_3d
