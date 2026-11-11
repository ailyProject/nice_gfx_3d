#pragma once
#include <NICE_GFX_3D.h>

// Add demo.scene to your application scene, or render it directly. This owns
// all borrowed arrays and keeps clips alive longer than its mixer.
struct KeyframeAnimationDemo {
  using Status=nice_gfx_3d::Status;
  nice_gfx_3d::Scene scene;
  nice_gfx_3d::BoxGeometry geometry;
  nice_gfx_3d::MeshBasicMaterial material{0x3dff};
  nice_gfx_3d::Mesh mesh{geometry,material};
  const float times[3]={0,1,2};
  const float positions[9]={0,-.3f,0, 0,.3f,0, 0,-.3f,0};
  nice_gfx_3d::KeyframeTrack track;
  nice_gfx_3d::AnimationClip clip;
  nice_gfx_3d::AnimationMixer mixer{scene};
  KeyframeAnimationDemo() {
    scene.add(mesh);
    track.target=&mesh;track.property=nice_gfx_3d::TrackProperty::Position;
    track.times=times;track.values=positions;track.keyCount=3;
    clip=nice_gfx_3d::AnimationClip("float",2,&track,1);
    mixer.clipAction(clip).setLoop(nice_gfx_3d::LoopRepeat).play();
  }
  void update(float deltaSeconds) {mixer.update(deltaSeconds);mesh.rotation.y+=deltaSeconds*.7f;}
};
