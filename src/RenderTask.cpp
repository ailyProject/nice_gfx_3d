#include "RenderTask.h"
#include <new>
#if NICE_GFX_3D_ESP
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#endif

namespace nice_gfx_3d {
struct RenderTask::Worker {
#if NICE_GFX_3D_ESP
  TaskHandle_t task=nullptr;
  SemaphoreHandle_t completion=nullptr;
#endif
};

RenderTask::~RenderTask(){end();}
Status RenderTask::begin(Renderer& renderer,Object3D& scene,Camera& camera,
                         const RenderTaskOptions& options){
  if(busy_)return Status::Busy;
  if(options.core<-1||options.core>1||(options.worker&&options.stackBytes<4096))return Status::InvalidArgument;
#if NICE_GFX_3D_ESP
  if(options.worker&&options.priority>=configMAX_PRIORITIES)return Status::InvalidArgument;
#if CONFIG_FREERTOS_UNICORE
  if(options.worker&&options.core==1)return Status::InvalidArgument;
#endif
#endif
  if(renderer_==&renderer&&scene_==&scene&&camera_==&camera&&
      options_.worker==options.worker&&options_.stackBytes==options.stackBytes&&
      options_.priority==options.priority&&options_.core==options.core)return Status::Ok;
  end();renderer_=&renderer;scene_=&scene;camera_=&camera;options_=options;
  if(!options.worker)return Status::Ok;
#if NICE_GFX_3D_ESP
  void* memory=heap_caps_malloc(sizeof(Worker),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!memory){workerStatus_=Status::NoMemory;return Status::Ok;}
  auto* worker=new(memory) Worker;
  worker->completion=xSemaphoreCreateBinary();
  BaseType_t core=options.core;
#if CONFIG_FREERTOS_UNICORE
  if(core<0)core=0;
#else
  if(core<0)core=xPortGetCoreID()==0?1:0;
#endif
  // Install before starting the task. It does not access the owner until a
  // notification is consumed and jobReady_ acquires the submitted input.
  worker_=worker;
  if(xTaskCreatePinnedToCoreWithCaps(run,"nice-gfx-3d",options.stackBytes,this,
      options.priority,&worker->task,core,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT|MALLOC_CAP_DMA)!=pdPASS){
    if(worker->completion)vSemaphoreDelete(worker->completion);
    worker->~Worker();heap_caps_free(worker);worker_=nullptr;
    workerStatus_=Status::NoMemory;
  }else workerStatus_=Status::Ok;
#else
  workerStatus_=Status::Unsupported;
#endif
  return Status::Ok;
}

Status RenderTask::submit(const RenderSurface* target){
  if(!ready())return Status::NotInitialized;
  if(busy_)return Status::Busy;
  if(!renderer_->ready())return Status::NotInitialized;
  if(target&&!target->valid())return Status::InvalidArgument;
#if NICE_GFX_3D_ESP
  if(worker_&&worker_->completion)while(xSemaphoreTake(worker_->completion,0)==pdTRUE){}
#endif
  target_=target?*target:RenderSurface{};hasTarget_=target!=nullptr;
  complete_.store(false,std::memory_order_relaxed);busy_=true;
  if(worker_){
    jobReady_.store(true,std::memory_order_release);
#if NICE_GFX_3D_ESP
    xTaskNotifyGive(worker_->task);
#endif
  }else{
    execute();complete_.store(true,std::memory_order_release);
  }
  return Status::Ok;
}

void RenderTask::execute(){
  const uint64_t started=detail::microseconds();
  result_=Result{};
  result_.status=hasTarget_?renderer_->renderToSurface(*scene_,*camera_,target_):renderer_->render(*scene_,*camera_);
  result_.elapsedMicros=detail::microseconds()-started;
  result_.stats=renderer_->stats();
#if NICE_GFX_3D_ESP
  if(worker_){
    result_.stackAddress=reinterpret_cast<uintptr_t>(&started);
    result_.stackFreeBytes=uint32_t(uxTaskGetStackHighWaterMark(nullptr));
  }
#endif
}

bool RenderTask::takeResult(Result& out){
  if(!complete())return false;
  out=result_;busy_=false;return true;
}
void RenderTask::waitForCompletion(){
  if(!busy_||complete())return;
#if NICE_GFX_3D_ESP
  if(worker_&&worker_->completion)xSemaphoreTake(worker_->completion,1);
  else vTaskDelay(1);
#endif
}
void RenderTask::end(){
#if NICE_GFX_3D_ESP
  if(worker_){
    while(busy_&&!complete())vTaskDelay(1);
    vTaskDeleteWithCaps(worker_->task);
    if(worker_->completion)vSemaphoreDelete(worker_->completion);
    worker_->~Worker();heap_caps_free(worker_);worker_=nullptr;
  }
#endif
  renderer_=nullptr;scene_=nullptr;camera_=nullptr;target_={};
  hasTarget_=busy_=false;jobReady_.store(false,std::memory_order_relaxed);
  complete_.store(false,std::memory_order_relaxed);workerStatus_=Status::NotInitialized;result_=Result{};
}
void RenderTask::run(void* context){
#if NICE_GFX_3D_ESP
  auto& self=*static_cast<RenderTask*>(context);
  for(;;){
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    if(!self.jobReady_.exchange(false,std::memory_order_acquire))continue;
    self.execute();
    // Finish every semaphore access before publication. The owner may end()
    // and delete the semaphore immediately after observing complete().
    if(self.worker_->completion)xSemaphoreGive(self.worker_->completion);
    self.complete_.store(true,std::memory_order_release);
  }
#else
  (void)context;
#endif
}
}
