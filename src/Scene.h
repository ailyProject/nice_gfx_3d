#pragma once
#include "Geometry.h"
#include "Material.h"
#include <vector>

namespace nice_gfx_3d {
enum class ObjectType:uint8_t {Object3D,Group,Scene,Mesh,SkinnedMesh,PerspectiveCamera,OrthographicCamera,AmbientLight,DirectionalLight};

// Scene links, geometry/material references and skeleton links never transfer
// ownership. Disconnect objects before destroying them. No allocation in updates.
class Object3D {
  bool quaternionMode_=false;
  bool transformDirty_=true;
  Vector3 previousPosition_,previousScale_{1,1,1};
  Vector3 previousRotation_;
  Quaternion previousQuaternion_;
 protected:
  virtual bool cameraObject()const{return false;}
 public:
  const char* name=nullptr; // borrowed optional label
  Vector3 position,rotation,scale{1,1,1};
  Quaternion quaternion;
  Matrix4 matrix,matrixWorld;
  Object3D* parent=nullptr;
  std::vector<Object3D*> children;
  bool visible=true,matrixAutoUpdate=true;
  Object3D()=default;
  Object3D(const Object3D&)=delete;Object3D&operator=(const Object3D&)=delete;
  Object3D(Object3D&&)=delete;Object3D&operator=(Object3D&&)=delete;
  virtual ~Object3D(){if(parent)parent->remove(*this);for(auto* child:children)if(child&&child->parent==this)child->parent=nullptr;}
  virtual ObjectType type()const{return ObjectType::Object3D;}
  bool add(Object3D& child){
    if(&child==this)return false;for(auto* p=this;p;p=p->parent)if(p==&child)return false;
    if(child.parent==this)return true;
    // Allocate before unlinking so allocation failure leaves existing tree intact.
    children.push_back(&child);if(child.parent)child.parent->remove(child);child.parent=this;return true;
  }
  bool add(Object3D* child){return child&&add(*child);}
  bool remove(Object3D& child){const auto it=std::find(children.begin(),children.end(),&child);if(it==children.end())return false;children.erase(it);if(child.parent==this)child.parent=nullptr;return true;}
  bool remove(Object3D* child){return child&&remove(*child);}
  Object3D& useQuaternion(bool enabled=true){quaternionMode_=enabled;previousRotation_=rotation;previousQuaternion_=quaternion;transformDirty_=true;return *this;}
  Object3D& setQuaternion(const Quaternion& value){quaternion=value;quaternion.normalize();return useQuaternion();}
  Object3D& setRotation(const Vector3& value){rotation=value;quaternionMode_=false;previousRotation_=rotation;quaternion.setFromEuler(rotation);previousQuaternion_=quaternion;transformDirty_=true;return *this;}
  virtual void updateMatrix(){
    const bool eulerChanged=rotation.x!=previousRotation_.x||rotation.y!=previousRotation_.y||rotation.z!=previousRotation_.z;
    const bool quaternionChanged=quaternion.x!=previousQuaternion_.x||quaternion.y!=previousQuaternion_.y||quaternion.z!=previousQuaternion_.z||quaternion.w!=previousQuaternion_.w;
    const bool positionChanged=position.x!=previousPosition_.x||position.y!=previousPosition_.y||position.z!=previousPosition_.z;
    const bool scaleChanged=scale.x!=previousScale_.x||scale.y!=previousScale_.y||scale.z!=previousScale_.z;
    if(!transformDirty_&&!eulerChanged&&!quaternionChanged&&!positionChanged&&!scaleChanged)return;
    if(eulerChanged)quaternionMode_=false;else if(quaternionChanged)quaternionMode_=true;
    if(!quaternionMode_&&(transformDirty_||eulerChanged))quaternion.setFromEuler(rotation);
    else if(quaternionMode_&&(transformDirty_||quaternionChanged))quaternion.normalize();
    previousPosition_=position;previousScale_=scale;previousRotation_=rotation;previousQuaternion_=quaternion;matrix.compose(position,quaternion,scale);transformDirty_=false;
  }
  virtual void updateMatrixWorld(bool force=false){(void)force;if(matrixAutoUpdate)updateMatrix();if(parent)matrixWorld.multiplyMatrices(parent->matrixWorld,matrix);else matrixWorld=matrix;for(auto*child:children)if(child)child->updateMatrixWorld(force);}
  // Update ancestors first when an object (not necessarily the scene root) is used.
  void updateWorldMatrix(bool updateParents,bool updateChildren){if(updateParents&&parent)parent->updateWorldMatrix(true,false);if(matrixAutoUpdate)updateMatrix();if(parent)matrixWorld.multiplyMatrices(parent->matrixWorld,matrix);else matrixWorld=matrix;if(updateChildren)for(auto*child:children)if(child)child->updateWorldMatrix(false,true);}
  Vector3 getWorldPosition(){updateWorldMatrix(true,false);return {matrixWorld.elements[12],matrixWorld.elements[13],matrixWorld.elements[14]};}
  Object3D& lookAt(const Vector3& target){
    updateWorldMatrix(true,false);const Vector3 eye(matrixWorld.elements[12],matrixWorld.elements[13],matrixWorld.elements[14]);
    Vector3 z=cameraObject()?eye-target:target-eye;if(z.lengthSq()<1e-12f)z.z=1;z.normalize();Vector3 up(0,1,0),x;x.crossVectors(up,z);if(x.lengthSq()<1e-12f){up.set(0,0,1);x.crossVectors(up,z);}x.normalize();Vector3 y;y.crossVectors(z,x);
    Matrix4 worldRotation;auto*e=worldRotation.elements;e[0]=x.x;e[1]=x.y;e[2]=x.z;e[4]=y.x;e[5]=y.y;e[6]=y.z;e[8]=z.x;e[9]=z.y;e[10]=z.z;
    Quaternion q;q.setFromRotationMatrix(worldRotation);
    if(parent){Matrix4 parentRotation=parent->matrixWorld;for(size_t c=0;c<3;++c){Vector3 axis(parentRotation.elements[c*4],parentRotation.elements[c*4+1],parentRotation.elements[c*4+2]);axis.normalize();parentRotation.elements[c*4]=axis.x;parentRotation.elements[c*4+1]=axis.y;parentRotation.elements[c*4+2]=axis.z;}Quaternion p;p.setFromRotationMatrix(parentRotation).invert();q.multiplyQuaternions(p,q);}
    return setQuaternion(q);
  }
  Object3D& lookAt(float x,float y,float z){return lookAt(Vector3(x,y,z));}
};
class Group:public Object3D {public:ObjectType type()const override{return ObjectType::Group;}};
class Scene:public Object3D {public:ObjectType type()const override{return ObjectType::Scene;}};
class Mesh:public Object3D {
 public:
  BufferGeometry* geometry=nullptr;Material* material=nullptr;
  Mesh()=default;Mesh(BufferGeometry& g,Material& m):geometry(&g),material(&m){}
  Mesh(BufferGeometry* g,Material* m):geometry(g),material(m){}
  ObjectType type()const override{return ObjectType::Mesh;}
};
class Camera:public Object3D {
 protected:bool cameraObject()const override{return true;}
 public:
  Matrix4 projectionMatrix,matrixWorldInverse;
  virtual void updateProjectionMatrix()=0;
  void updateMatrixWorld(bool force=false)override{if(parent)parent->updateWorldMatrix(true,false);Object3D::updateMatrixWorld(force);matrixWorldInverse=matrixWorld;matrixWorldInverse.invert();}
};
class PerspectiveCamera:public Camera {
 public:
  float fov=50,aspect=1,near=0.1f,far=1000;
  PerspectiveCamera(float fovDegrees=50,float aspectRatio=1,float nearPlane=.1f,float farPlane=1000):fov(fovDegrees),aspect(aspectRatio),near(nearPlane),far(farPlane){updateProjectionMatrix();}
  ObjectType type()const override{return ObjectType::PerspectiveCamera;}
  void updateProjectionMatrix()override{projectionMatrix.makePerspective(fov,aspect,near,far);}
};
class OrthographicCamera:public Camera {
 public:
  float left=-1,right=1,top=1,bottom=-1,near=.1f,far=1000;
  OrthographicCamera(float l=-1,float r=1,float t=1,float b=-1,float n=.1f,float f=1000):left(l),right(r),top(t),bottom(b),near(n),far(f){updateProjectionMatrix();}
  ObjectType type()const override{return ObjectType::OrthographicCamera;}
  void updateProjectionMatrix()override{projectionMatrix.makeOrthographic(left,right,top,bottom,near,far);}
};
class Light:public Object3D {public:uint16_t color=0xffff;float intensity=1;Light(uint16_t c=0xffff,float i=1):color(c),intensity(i){}};
class AmbientLight:public Light {public:AmbientLight(uint16_t c=0xffff,float i=1):Light(c,i){}ObjectType type()const override{return ObjectType::AmbientLight;}};
class DirectionalLight:public Light {
 public:
  // Local direction in which the light rays travel, transformed by matrixWorld.
  Vector3 direction{0,0,-1};
  DirectionalLight(uint16_t c=0xffff,float i=1):Light(c,i){}
  ObjectType type()const override{return ObjectType::DirectionalLight;}
};

class Skeleton {
 public:
  std::vector<Object3D*> bones;
  std::vector<Matrix4> boneInverses,boneMatrices;
  Skeleton()=default;
  Skeleton(const Skeleton&)=delete;Skeleton&operator=(const Skeleton&)=delete;
  // Call during setup. Inverses omitted: snapshot current bone world transforms.
  void setBones(const std::vector<Object3D*>& value,const std::vector<Matrix4>& inverses={}){bones=value;boneMatrices.resize(bones.size());boneInverses=inverses;if(boneInverses.size()!=bones.size())computeInverses();}
  void computeInverses(){boneInverses.resize(bones.size());boneMatrices.resize(bones.size());for(size_t i=0;i<bones.size();++i){if(bones[i]){bones[i]->updateWorldMatrix(true,false);boneInverses[i]=bones[i]->matrixWorld;boneInverses[i].invert();}else boneInverses[i].identity();}}
  // Scene world matrices must be current. A structural mismatch never allocates.
  bool update(){if(boneMatrices.size()!=bones.size()||boneInverses.size()!=bones.size())return false;for(size_t i=0;i<bones.size();++i)if(bones[i])boneMatrices[i].multiplyMatrices(bones[i]->matrixWorld,boneInverses[i]);else boneMatrices[i].identity();return true;}
};
class SkinnedMesh:public Mesh {
  std::vector<Matrix4> skinMatrices_,normalMatrices_;
  bool paletteReady_=false;
  bool affinePalette_=false;
  static bool isAffine(const Matrix4& matrix){
    const auto* e=matrix.elements;
    return e[3]==0&&e[7]==0&&e[11]==0&&e[15]==1;
  }
  // Affine inverse-transpose needs only the 3x3 cofactors used by normals.
  // Preserve the pivoted generic inverse for ill-conditioned/non-affine input.
  static bool affineNormalMatrix(const Matrix4& matrix,Matrix4& output){
    const auto* e=matrix.elements;
    const float c0=e[5]*e[10]-e[9]*e[6],c1=e[9]*e[2]-e[1]*e[10],c2=e[1]*e[6]-e[5]*e[2];
    const float t0=e[0]*c0,t1=e[4]*c1,t2=e[8]*c2,det=t0+t1+t2;
    const float magnitude=std::fabs(t0)+std::fabs(t1)+std::fabs(t2);
    if(!std::isfinite(det)||!std::isfinite(magnitude)||!(std::fabs(det)>magnitude*1e-6f))return false;
    const float inv=1.0f/det;
    const float n[9]={c0*inv,c1*inv,c2*inv,
      (e[8]*e[6]-e[4]*e[10])*inv,(e[0]*e[10]-e[8]*e[2])*inv,(e[4]*e[2]-e[0]*e[6])*inv,
      (e[4]*e[9]-e[8]*e[5])*inv,(e[8]*e[1]-e[0]*e[9])*inv,(e[0]*e[5]-e[4]*e[1])*inv};
    for(float value:n)if(!std::isfinite(value))return false;
    output.identity();
    // n stores columns of inverse(A), which are rows of inverse-transpose(A).
    for(size_t r=0;r<3;++r)for(size_t c=0;c<3;++c)output.elements[c*4+r]=n[r*3+c];
    return true;
  }
 public:
  Skeleton* skeleton=nullptr;Matrix4 bindMatrix,bindMatrixInverse;
  // Default glTF palette: inverse(meshWorld) * jointWorld * inverseBind.
  // A supplied bindMatrix additionally transforms input local positions. Set
  // useMeshWorldInverse=false for the fixed bindMatrixInverse space convention.
  bool useMeshWorldInverse=true;
  using Mesh::Mesh;
  ObjectType type()const override{return ObjectType::SkinnedMesh;}
  bool bind(Skeleton& value,const Matrix4& bind=Matrix4()){
    Matrix4 inverse=bind;if(!inverse.tryInvert())return false;
    paletteReady_=false;skinMatrices_.resize(value.bones.size());normalMatrices_.resize(value.bones.size());skeleton=&value;bindMatrix=bind;bindMatrixInverse=inverse;return true;
  }
  // Call once per mesh per render after updating scene world matrices. No heap.
  // Bone-count changes require an explicit rebind during setup.
  bool prepareSkinning(){
    paletteReady_=false;affinePalette_=false;if(!skeleton||skinMatrices_.size()!=skeleton->bones.size()||normalMatrices_.size()!=skeleton->bones.size()||!skeleton->update())return false;
    Matrix4 inverse=useMeshWorldInverse?matrixWorld:bindMatrixInverse;if(useMeshWorldInverse&&!inverse.tryInvert())return false;
    bool allAffine=true;
    for(size_t i=0;i<skinMatrices_.size();++i){
      skinMatrices_[i].multiplyMatrices(inverse,skeleton->boneMatrices[i]).multiply(bindMatrix);
      const bool affine=isAffine(skinMatrices_[i]);allAffine=allAffine&&affine;
      if(affine&&affineNormalMatrix(skinMatrices_[i],normalMatrices_[i]))continue;
      normalMatrices_[i]=skinMatrices_[i];
      if(normalMatrices_[i].tryInvert())normalMatrices_[i].transpose();
      // Animated zero bone scale is valid. Its collapsed position transform
      // still applies; the linear normal fallback avoids failing the frame.
      else normalMatrices_[i]=skinMatrices_[i];
    }
    affinePalette_=allAffine;paletteReady_=true;return true;
  }
  NICE_GFX_3D_HOT_INLINE Vertex skinVertex(size_t index,const Vertex& vertex)const{
    return skinVertexImpl<true>(index,vertex);
  }
 private:
  friend class Renderer;
  // Renderer performs the final world-space normalization for Lambert shading.
  // Public skinVertex keeps its original normalized-normal contract.
  NICE_GFX_3D_HOT_INLINE Vertex skinVertexUnnormalizedNormal(size_t index,const Vertex& vertex)const{
    return skinVertexImpl<false>(index,vertex);
  }
  template<bool normalizeNormal,bool transformPosition=true,bool transformNormal=true>
  NICE_GFX_3D_HOT_INLINE Vertex skinVertexImpl(size_t index,const Vertex& vertex,uint16_t cachedRigidJoint=UINT16_MAX)const{
    if(!paletteReady_||!geometry||!geometry->skinVertices||index>=geometry->vertexCount)return vertex;
    // Exact common case: the other slots are inactive, including negative zero.
    // Read the public skin data every call; edits never require cache invalidation.
    // Every other weight/joint combination keeps the general filtering below.
    const auto& rigid=geometry->skinVertices[index];
    const size_t joint=cachedRigidJoint!=UINT16_MAX?cachedRigidJoint:rigid.joints[0];
    // Only the renderer's immutable Model cache supplies this hint. Default
    // calls preserve the ordinary public skin path; invalid/sentinel joints
    // retain the exact general filtering path below.
    if((cachedRigidJoint!=UINT16_MAX||(rigid.weights[0]==1.0f&&rigid.weights[1]==0.0f&&rigid.weights[2]==0.0f&&rigid.weights[3]==0.0f))&&joint<skinMatrices_.size()){
      Vertex result=vertex;
      if constexpr(transformPosition){
        Vector3 p=vertex.position;
        if(affinePalette_){const auto* m=skinMatrices_[joint].elements;const auto& v=vertex.position;
          p.set(m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]);}
        else p.applyMatrix4(skinMatrices_[joint]);
        // Preserve +0 + p*1 rather than copying p: signed zero is observable.
        Vector3 positionSum;positionSum.addScaledVector(p,1.0f);
        result.position=positionSum.multiplyScalar(1.0f);
      }
      if constexpr(transformNormal){
        const auto* e=normalMatrices_[joint].elements;const auto& n=vertex.normal;Vector3 transformed(e[0]*n.x+e[4]*n.y+e[8]*n.z,e[1]*n.x+e[5]*n.y+e[9]*n.z,e[2]*n.x+e[6]*n.y+e[10]*n.z);
        Vector3 normalSum;normalSum.addScaledVector(transformed,1.0f);
        result.normal=normalSum;if(normalizeNormal)result.normal.normalize();
      }
      return result;
    }
    const auto& skin=geometry->skinVertices[index];float total=0;Vector3 positionSum,normalSum;
    for(size_t i=0;i<4;++i){const float w=skin.weights[i];const size_t joint=skin.joints[i];if(!(w>0)||!std::isfinite(w)||joint>=skinMatrices_.size())continue;
      if constexpr(transformPosition){
        Vector3 p=vertex.position;
        if(affinePalette_){const auto* m=skinMatrices_[joint].elements;const auto& v=vertex.position;
          p.set(m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]);}
        else p.applyMatrix4(skinMatrices_[joint]);
        positionSum.addScaledVector(p,w);
      }
      if constexpr(transformNormal){
        const auto* e=normalMatrices_[joint].elements;const auto& n=vertex.normal;Vector3 transformed(e[0]*n.x+e[4]*n.y+e[8]*n.z,e[1]*n.x+e[5]*n.y+e[9]*n.z,e[2]*n.x+e[6]*n.y+e[10]*n.z);normalSum.addScaledVector(transformed,w);
      }
      total+=w;
    }
    if(!(total>0)||!std::isfinite(total))return vertex;
    Vertex result=vertex;
    if constexpr(transformPosition)result.position=positionSum.multiplyScalar(1/total);
    if constexpr(transformNormal){result.normal=normalSum;if(normalizeNormal)result.normal.normalize();}
    return result;
  }
};
} // namespace nice_gfx_3d
