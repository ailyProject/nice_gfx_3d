#pragma once
#include "Scene.h"
#include <memory>
#include <vector>

namespace nice_gfx_3d {
enum class TrackProperty:uint8_t {Position,Quaternion,Scale};
enum class Interpolation:uint8_t {Linear,Step};
enum class LoopMode:uint8_t {Once,Repeat,PingPong};
constexpr LoopMode LoopOnce=LoopMode::Once,LoopRepeat=LoopMode::Repeat,LoopPingPong=LoopMode::PingPong;

// Borrowed targets, sorted finite seconds and packed 3f/4f values. All storage
// (including clip/name) must outlive its actions; destroy mixers before models.
struct KeyframeTrack {
  Object3D* target=nullptr;
  TrackProperty property=TrackProperty::Position;
  const float* times=nullptr;
  const float* values=nullptr;
  size_t keyCount=0;
  Interpolation interpolation=Interpolation::Linear;
  bool valid()const{
    if(!target||!times||!values||!keyCount)return false;
    float previous=-1;const size_t components=property==TrackProperty::Quaternion?4:3;
    if(keyCount>SIZE_MAX/components)return false;
    for(size_t i=0;i<keyCount;++i){if(!std::isfinite(times[i])||times[i]<0||times[i]<previous)return false;previous=times[i];for(size_t c=0;c<components;++c)if(!std::isfinite(values[i*components+c]))return false;}return true;
  }
};
struct AnimationClip {
  const char* name=nullptr;
  float duration=0;
  const KeyframeTrack* tracks=nullptr;
  size_t trackCount=0;
  AnimationClip()=default;
  AnimationClip(const char* n,float seconds,const KeyframeTrack* data,size_t count):name(n),duration(seconds),tracks(data),trackCount(count){}
  bool valid()const{if(!std::isfinite(duration)||duration<0||(trackCount&&!tracks))return false;for(size_t i=0;i<trackCount;++i)if(!tracks[i].valid()||tracks[i].times[tracks[i].keyCount-1]>duration)return false;return true;}
};

class AnimationMixer;
class AnimationAction {
  friend class AnimationMixer;
  const AnimationClip* clip_=nullptr;
  double elapsed_=0;
  bool playing_=false,finished_=false,valid_=false;
  float sampleTime_=0;
  explicit AnimationAction(const AnimationClip& clip):clip_(&clip),valid_(clip.valid()){}
  void advance(float delta){
    if(!playing_||paused||!enabled||finished_||!valid_)return;
    elapsed_+=double(delta)*double(timeScale);if(!std::isfinite(elapsed_)){playing_=false;return;}
    const double duration=clip_->duration;
    if(duration<=0){sampleTime_=0;finished_=true;return;}
    const double cycles=loop==LoopMode::Once?1.0:(repetitions<0?-1.0:double(std::max(1,repetitions)));
    bool ended=false;double at=elapsed_;
    if(at<0){at=0;elapsed_=0;ended=true;}
    if(cycles>0&&at>=duration*cycles){at=duration*cycles;ended=true;}
    if(loop==LoopMode::Once){sampleTime_=float(std::min(duration,at));}
    else {
      double cycle=std::floor(at/duration),within=at-cycle*duration;
      // A finite ending samples the final endpoint, not the next loop's start.
      if(ended&&at>0&&within==0){cycle-=1;within=duration;}
      if(loop==LoopMode::PingPong&&std::fmod(cycle,2.0)>=1)within=duration-within;
      sampleTime_=float(within);
    }
    if(ended)finished_=true;
    time=sampleTime_;
  }
 public:
  float time=0,timeScale=1,weight=1;
  LoopMode loop=LoopMode::Repeat;
  int repetitions=-1;
  bool enabled=true,paused=false,clampWhenFinished=true;
  AnimationAction(const AnimationAction&)=delete;AnimationAction&operator=(const AnimationAction&)=delete;
  AnimationAction& play(){if(valid_){playing_=true;enabled=true;}return *this;}
  // The next mixer update restores bindings left without any active action.
  AnimationAction& stop(){playing_=false;return reset();}
  AnimationAction& reset(){elapsed_=0;time=sampleTime_=0;finished_=false;paused=false;return *this;}
  AnimationAction& setLoop(LoopMode mode,int count=-1){loop=mode;repetitions=count;return *this;}
  AnimationAction& setEffectiveWeight(float value){weight=std::isfinite(value)?std::max(0.0f,value):0;return *this;}
  AnimationAction& setEffectiveTimeScale(float value){timeScale=std::isfinite(value)?value:0;return *this;}
  AnimationAction& setTime(float seconds){if(std::isfinite(seconds)){elapsed_=std::max(0.0,double(seconds));sampleTime_=time=std::min(std::max(0.0f,seconds),clip_->duration);finished_=false;}return *this;}
  bool isRunning()const{return valid_&&playing_&&enabled&&!paused&&!finished_;}
  bool isFinished()const{return finished_;}
  bool valid()const{return valid_;}
  const AnimationClip& getClip()const{return *clip_;}
};

class AnimationMixer {
  struct Binding {
    Object3D* target=nullptr;TrackProperty property=TrackProperty::Position;
    bool applied=false;
    float base[4]={0,0,0,1},sum[4]={0,0,0,0},total=0;
  };
  Object3D* root_=nullptr;
  // Heap allocations happen only while registering actions/bindings. Individual
  // action allocations preserve references across later clipAction() calls.
  std::vector<std::unique_ptr<AnimationAction>> actions_;
  std::vector<Binding> bindings_;
  Binding* findBinding(Object3D* target,TrackProperty property){for(auto& binding:bindings_)if(binding.target==target&&binding.property==property)return &binding;return nullptr;}
  static void sample(const KeyframeTrack& track,float time,float* out){
    const size_t components=track.property==TrackProperty::Quaternion?4:3;
    size_t lo=0,hi=track.keyCount;
    while(lo<hi){const size_t mid=lo+(hi-lo)/2;if(track.times[mid]<=time)lo=mid+1;else hi=mid;}
    const size_t a=lo?lo-1:0,b=std::min(a+1,track.keyCount-1);float t=0;
    if(track.interpolation==Interpolation::Linear&&b!=a&&time>track.times[a]&&track.times[b]>track.times[a])t=clamp((time-track.times[a])/(track.times[b]-track.times[a]),0,1);
    const float* va=track.values+a*components;const float* vb=track.values+b*components;
    if(components==4){Quaternion q(va[0],va[1],va[2],va[3]);q.normalize().slerp(Quaternion(vb[0],vb[1],vb[2],vb[3]).normalize(),t);out[0]=q.x;out[1]=q.y;out[2]=q.z;out[3]=q.w;}
    // Step and exact endpoints must not evaluate an unused overflowing delta.
    else {for(size_t c=0;c<3;++c)out[c]=t==0?va[c]:va[c]+(vb[c]-va[c])*t;out[3]=0;}
  }
  static Binding makeBinding(const KeyframeTrack& track){
    Binding b;b.target=track.target;b.property=track.property;
    if(track.property==TrackProperty::Quaternion){track.target->updateMatrix();const auto&q=track.target->quaternion;b.base[0]=q.x;b.base[1]=q.y;b.base[2]=q.z;b.base[3]=q.w;}
    else {const auto&v=track.property==TrackProperty::Position?track.target->position:track.target->scale;b.base[0]=v.x;b.base[1]=v.y;b.base[2]=v.z;b.base[3]=0;}return b;
  }
  static void apply(Binding& b){
    const bool contributing=b.total>0;
    if(!contributing&&!b.applied)return;
    // Restore once when the last action stops contributing. Leave subsequent
    // application edits alone while this binding remains inactive.
    b.applied=contributing;
    const float remaining=std::max(0.0f,1-b.total),divisor=b.total+remaining;
    if(b.property==TrackProperty::Quaternion){
      float baseWeight=remaining;
      if(remaining>0){float dot=0;for(size_t c=0;c<4;++c)dot+=b.sum[c]*b.base[c];if(dot<0)baseWeight=-remaining;}
      Quaternion q;float v[4];for(size_t c=0;c<4;++c)v[c]=(b.sum[c]+b.base[c]*baseWeight)/divisor;q.set(v[0],v[1],v[2],v[3]).normalize();b.target->setQuaternion(q);
    }
    else {Vector3 v((b.sum[0]+b.base[0]*remaining)/divisor,(b.sum[1]+b.base[1]*remaining)/divisor,(b.sum[2]+b.base[2]*remaining)/divisor);if(b.property==TrackProperty::Position)b.target->position=v;else b.target->scale=v;}
  }
 public:
  float timeScale=1;
  explicit AnimationMixer(Object3D& root):root_(&root){}
  AnimationMixer()=default;
  AnimationMixer(const AnimationMixer&)=delete;AnimationMixer&operator=(const AnimationMixer&)=delete;
  Object3D* getRoot()const{return root_;}
  AnimationAction& clipAction(const AnimationClip& clip){
    for(auto& action:actions_)if(action->clip_==&clip)return *action;
    auto action=std::unique_ptr<AnimationAction>(new AnimationAction(clip));
    if(action->valid_)for(size_t i=0;i<clip.trackCount;++i){const auto& track=clip.tracks[i];if(!findBinding(track.target,track.property))bindings_.push_back(makeBinding(track));}
    actions_.push_back(std::move(action));return *actions_.back();
  }
  AnimationMixer& stopAllAction(){for(auto&action:actions_)action->stop();return *this;}
  // Zero allocations. deltaSeconds may be negative for reverse playback.
  void update(float deltaSeconds){
    if(!std::isfinite(deltaSeconds)||!std::isfinite(timeScale))return;
    const float delta=deltaSeconds*timeScale;if(!std::isfinite(delta))return;
    for(auto& binding:bindings_){binding.total=0;for(float& v:binding.sum)v=0;}
    for(auto& owned:actions_){auto&action=*owned;action.advance(delta);
      if(!action.valid_||!action.playing_||!action.enabled||!(action.weight>0)||!std::isfinite(action.weight)||(action.finished_&&!action.clampWhenFinished))continue;
      const auto&clip=*action.clip_;for(size_t i=0;i<clip.trackCount;++i){const auto&track=clip.tracks[i];auto*b=findBinding(track.target,track.property);if(!b)continue;float value[4];sample(track,action.sampleTime_,value);
        float sign=1;if(track.property==TrackProperty::Quaternion){
          // Align additional actions to the blended pose, so rotations either
          // side of 180 degrees take their short arc rather than cancelling.
          const float* reference=b->total>0?b->sum:b->base;
          float dot=0;for(size_t c=0;c<4;++c)dot+=value[c]*reference[c];if(dot<0)sign=-1;
        }
        for(size_t c=0;c<4;++c)b->sum[c]+=value[c]*action.weight*sign;b->total+=action.weight;
      }
    }
    for(auto&binding:bindings_)apply(binding);
  }
};
} // namespace nice_gfx_3d
