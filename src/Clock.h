#pragma once
#include "Memory.h"

namespace nice_gfx_3d {
class Clock {
 public:
  explicit Clock(bool autoStart=true):autoStart_(autoStart){}
  void start(){previous_=detail::microseconds();elapsed_=0;running_=true;}
  void stop(){if(running_)getDelta();running_=false;autoStart_=false;}
  float getDelta(){
    if(!running_){if(autoStart_)start();return 0;}
    const uint64_t now=detail::microseconds();const float delta=float(now-previous_)*1e-6f;
    previous_=now;elapsed_+=delta;return delta;
  }
  double getElapsedTime(){getDelta();return elapsed_;}
  bool running()const{return running_;}
 private:
  bool autoStart_=true,running_=false;
  uint64_t previous_=0;
  double elapsed_=0;
};
}
