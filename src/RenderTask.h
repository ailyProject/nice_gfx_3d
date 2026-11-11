#pragma once
#include "Renderer.h"
#include <atomic>

#define NICE_GFX_3D_HAS_RENDER_TASK 1

namespace nice_gfx_3d {

struct RenderTaskOptions {
  // Explicit opt-in. The default and unsupported hosts execute synchronously.
  bool worker=false;
  uint32_t stackBytes=16384;
  uint8_t priority=1;
  int8_t core=-1; // -1 selects the other core; 0/1 select an explicit core.
};

// One owner submits and consumes results; an optional worker only renders.
// Renderer, scene, camera and borrowed target must outlive end()/destruction.
// Do not render separately, mutate their data, or release a target while busy().
// complete() publishes all render writes; takeResult() releases the owner's
// pending job, including failed jobs. Rendering never retries a failed frame.
class RenderTask {
 public:
  struct Result {
    Status status=Status::NotInitialized;
    uint64_t elapsedMicros=0;
    uint32_t stackFreeBytes=0; // Worker-only diagnostic; zero in synchronous mode.
    uintptr_t stackAddress=0;
    RenderStats stats;
  };
  RenderTask()=default;
  ~RenderTask();
  RenderTask(const RenderTask&)=delete;
  RenderTask& operator=(const RenderTask&)=delete;
  RenderTask(RenderTask&&)=delete;
  RenderTask& operator=(RenderTask&&)=delete;

  // Setup only. A failed worker allocation leaves a usable synchronous task:
  // begin returns Ok, available()==false, workerStatus() explains the fallback.
  // Invalid arguments and a pending job preserve the previous setup. Repeating
  // an identical idle begin does not allocate or restart the existing task.
  // The stable Renderer may be initialized later, reserving the optional worker
  // stack before renderer/model caches use internal RAM. submit then rejects an
  // uninitialized renderer without accepting a job or touching its frame data.
  Status begin(Renderer& renderer,Object3D& scene,Camera& camera,
               const RenderTaskOptions& options=RenderTaskOptions());
  bool ready()const{return renderer_!=nullptr;}
  bool available()const{return worker_!=nullptr;}
  Status workerStatus()const{return workerStatus_;}
  const RenderTaskOptions& options()const{return options_;}
  bool busy()const{return busy_;}
  bool complete()const{return busy_&&complete_.load(std::memory_order_acquire);}

  // Ok means one accepted job, not a successful render; inspect Result.status.
  // A synchronous job also remains busy/complete until its result is consumed.
  // Null target uses renderer.frame(); a nonnull target is borrowed unchanged.
  Status submit(const RenderSurface* target=nullptr);
  Status submit(const RenderSurface& target){return submit(&target);}
  bool takeResult(Result& out); // False leaves out unchanged.
  Result takeResult(){Result result;takeResult(result);return result;}
  // At most one RTOS tick. A wake can precede publication, so recheck complete().
  // Synchronous/idle tasks return immediately. end() is the unbounded join.
  void waitForCompletion();
  void end();

 private:
  struct Worker;
  Worker* worker_=nullptr;
  Renderer* renderer_=nullptr;
  Object3D* scene_=nullptr;
  Camera* camera_=nullptr;
  RenderTaskOptions options_;
  RenderSurface target_;
  bool hasTarget_=false,busy_=false; // Owner-task access only.
  std::atomic<bool> jobReady_{false},complete_{false};
  Status workerStatus_=Status::NotInitialized;
  Result result_;
  void execute();
  static void run(void* context);
};
}
