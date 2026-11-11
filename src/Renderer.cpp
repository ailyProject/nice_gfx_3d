// The Studio embeds these sources into Arduino sketches whose global flags
// often optimize for size. Keep this translation unit's raster loops optimized
// without changing application or public-header floating-point semantics.
#ifndef NICE_GFX_3D_OPTIMIZE_RENDERER
#define NICE_GFX_3D_OPTIMIZE_RENDERER 1
#endif
#if NICE_GFX_3D_OPTIMIZE_RENDERER && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O3")
#endif

#include "Renderer.h"
#if NICE_GFX_3D_ESP
#define NICE_GFX_3D_HOT_SHADER_SECTION __attribute__((section(".iram1"),noinline))
#else
#define NICE_GFX_3D_HOT_SHADER_SECTION
#endif
#include "FastClear.h"
#include "TintTables.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#if NICE_GFX_3D_ESP
#include <atomic>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#endif
#if NICE_GFX_3D_ESP && configNUMBER_OF_CORES > 1 && configSUPPORT_STATIC_ALLOCATION == 1 && INCLUDE_vTaskSuspend == 1
#define NICE_GFX_3D_RASTER_TASKS 1
#else
#define NICE_GFX_3D_RASTER_TASKS 0
#endif
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3
#include "driver/ppa.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6,1,0)
#include "esp_private/esp_cache_private.h"
#endif
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#endif

namespace nice_gfx_3d {
namespace {
// Clipped raster coordinates are finite and fit in a signed 32-bit integer.
// Convert directly with round-up; ceilf followed by an integer conversion
// otherwise spills live scanline registers across a library call on ESP32.
inline int rasterCeilToInt(float value){
#if defined(__riscv) && defined(__riscv_flen) && __riscv_flen >= 32
  int result;
  __asm__("fcvt.w.s %0, %1, rup" : "=r"(result) : "f"(value));
  return result;
#else
  return int(std::ceil(value));
#endif
}
// These are the same correctly rounded float values produced by channel/31 or
// channel/63 under the renderer's ordinary round-to-nearest environment.
template<unsigned Maximum> struct ChannelNormalization {
  float values[Maximum+1]={};
  constexpr ChannelNormalization(){
    for(unsigned i=0;i<=Maximum;++i)values[i]=float(i)/float(Maximum);
  }
};
constexpr ChannelNormalization<31> normalized5;
constexpr ChannelNormalization<63> normalized6;
inline float clamp01(float x) { return std::fmax(0.0f, std::fmin(1.0f, x)); }
inline void unpack(uint16_t c, float &r, float &g, float &b) {
  r=normalized5.values[c>>11];g=normalized6.values[(c>>5)&63];b=normalized5.values[c&31];
}
inline uint16_t pack(float r, float g, float b) {
  return uint16_t((uint32_t(clamp01(r) * 31.0f + 0.5f) << 11) |
      (uint32_t(clamp01(g) * 63.0f + 0.5f) << 5) | uint32_t(clamp01(b) * 31.0f + 0.5f));
}
inline uint16_t blend565(uint16_t foreground, uint16_t background, unsigned alpha) {
  if (alpha >= 255) return foreground;
  const unsigned inverse = 255 - alpha;
  const unsigned r = (((foreground >> 11) * alpha + (background >> 11) * inverse + 127) / 255);
  const unsigned g = ((((foreground >> 5) & 63) * alpha + ((background >> 5) & 63) * inverse + 127) / 255);
  const unsigned b = (((foreground & 31) * alpha + (background & 31) * inverse + 127) / 255);
  return uint16_t((r << 11) | (g << 5) | b);
}
inline bool finite3(const Vector3 &p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline bool transparent(const Material &m) { return m.transparent || m.opacity < 1.0f; }
inline float determinant3(const Matrix4 &matrix) {
  const float *m = matrix.elements;
  return m[0] * (m[5]*m[10]-m[9]*m[6]) - m[4] * (m[1]*m[10]-m[9]*m[2]) + m[8] * (m[1]*m[6]-m[5]*m[2]);
}
inline void fill565(uint16_t *pixels, size_t count, uint16_t color) {
  detail::fill565Cpu(pixels,count,color);
}
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3
bool clearCacheSync(void* pixels,size_t bytes,int flags){
  size_t line=0;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6,1,0)
  line=esp_cache_get_line_size_by_addr(pixels);
#else
  const uint32_t caps=esp_ptr_external_ram(pixels)?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL;
  if(esp_cache_get_alignment(caps,&line)!=ESP_OK)return false;
#endif
  if(!line)return true;
  if(reinterpret_cast<uintptr_t>(pixels)%line||bytes%line)return false;
  return esp_cache_msync(pixels,bytes,flags)==ESP_OK;
}
bool fill565Ppa(void* client,uint16_t* pixels,uint16_t width,uint16_t height,uint16_t color){
  const size_t bytes=size_t(width)*height*2;
  if(!client||!pixels||(reinterpret_cast<uintptr_t>(pixels)&63)||(bytes&63))return false;
  // The pinned ESP-IDF Fill driver writes back and invalidates this complete
  // output window before queueing DMA. The caller owns this buffer exclusively.
  ppa_fill_oper_config_t operation={};
  operation.out.buffer=pixels;
  operation.out.buffer_size=bytes;
  operation.out.pic_w=width;operation.out.pic_h=height;
  operation.out.fill_cm=PPA_FILL_COLOR_MODE_RGB565;
  operation.fill_block_w=width;operation.fill_block_h=height;
  // The public Fill API requires ARGB channels even for RGB565 output.
  const uint32_t argb=detail::ppaFillArgb8888FromRgb565(color);
  operation.fill_argb_color.a=0xff;
  operation.fill_argb_color.r=uint8_t(argb>>16);
  operation.fill_argb_color.g=uint8_t(argb>>8);
  operation.fill_argb_color.b=uint8_t(argb);
  operation.mode=PPA_TRANS_MODE_BLOCKING;
  if(ppa_do_fill(static_cast<ppa_client_handle_t>(client),&operation)!=ESP_OK)return false;
  // The blocking return transfers ownership back. CPU reads/updates the depth
  // and colour buffers only after invalidating any prefetched stale lines.
  return clearCacheSync(pixels,bytes,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
#endif
// Coordinates remain bounded after homogeneous clipping. Texel addressing is
// done only for fragments that survive depth testing.
inline uint32_t texelCoordinate(float coordinate, uint32_t extent, Wrap wrapping) {
  coordinate = Texture::wrap(coordinate, wrapping);
  return std::min(extent - 1, uint32_t(coordinate * float(extent)));
}
template<bool Tiled>
inline size_t textureOffset(uint32_t x,uint32_t y,const Texture& texture,uint32_t tileColumns){
  if constexpr(Tiled)return (size_t(y>>3)*tileColumns+(x>>3))*64+size_t(y&7)*8+(x&7);
  else return size_t(y)*texture.stride+x;
}
template<bool Tiled>
NICE_GFX_3D_HOT_INLINE uint16_t sampleTextureTile(uint32_t x,uint32_t y,const Texture& texture,
    const uint16_t* pixels,uint32_t columns,const uint32_t* compactBlocks){
  if constexpr(Tiled){
    const size_t tile=size_t(y>>3)*columns+(x>>3);
    const uint32_t within=(y&7)*8+(x&7);
    if(compactBlocks){
      const uint32_t block=compactBlocks[tile];if(block&0x80000000u)return uint16_t(block);
      const uint8_t* data=reinterpret_cast<const uint8_t*>(pixels)+(size_t(block&0x3fffffffu)<<6);
      if(block&0x40000000u){
        const unsigned packed=data[within>>1],index=(packed>>((within&1u)*4))&15u;
        return reinterpret_cast<const uint16_t*>(data+32)[index];
      }
      return reinterpret_cast<const uint16_t*>(data)[within];
    }
    return pixels[tile*64+within];
  }else return pixels[size_t(y)*texture.stride+x];
}
// The renderer accepts span lengths 1..32. Only power-of-two entries are used
// for multiplication below; odd spans retain their original floating division.
constexpr float spanReciprocals[33] = {
  0, 1, .5f, 0, .25f, 0, 0, 0, .125f, 0, 0, 0, 0, 0, 0, 0,
  .0625f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .03125f
};
bool sphereVisible(const Matrix4 &vp,const Vector3 &center,float radius) {
  const float *m=vp.elements;
  for(unsigned axis=0;axis<3;++axis) for(int sign:{-1,1}) {
    const float a=m[3]+sign*m[axis], b=m[7]+sign*m[4+axis];
    const float c=m[11]+sign*m[8+axis],d=m[15]+sign*m[12+axis];
    if(a*center.x+b*center.y+c*center.z+d < -radius*std::sqrt(a*a+b*b+c*c)) return false;
  }
  return true;
}
}

struct Renderer::ClipVertex {
  float x, y, z, w;
  float u, v, r, g, b;
  float inverseW;
  uint32_t clipMask;
};
struct Renderer::DrawItem {
  Mesh *mesh = nullptr;
  float depth = 0;
  size_t order = 0;
};

// The owner transforms vertices and snapshots material/texture descriptors.
// Both raster contexts only read this batch until the mesh barrier completes.
struct Renderer::RasterBatch {
  const ClipVertex* vertices;
  const uint32_t* indices;
  size_t vertexCount,indexCount;
  bool reverseWinding;
  bool screenProjected;
  Material material;
  Texture texture;
  detail::TextureTiles textureTiles;
  const uint16_t* closedTriangleComponents=nullptr;
  Side closedSides[128];

  RasterBatch(const ClipVertex* transformed,const BufferGeometry& geometry,
              const Material& source,bool reverse,bool projected)
    :vertices(transformed),indices(geometry.indices),vertexCount(geometry.vertexCount),
     indexCount(geometry.indices?geometry.indexCount:geometry.vertexCount),
     reverseWinding(reverse),screenProjected(projected),material(source) {
    if(source.map){
      texture=*source.map;
      const auto* tiles=source.map->tiles_;
      if(tiles&&tiles->pixels&&tiles->sourcePixels==texture.pixels&&
          tiles->width==texture.width&&tiles->height==texture.height&&tiles->stride==texture.stride){
        textureTiles=*tiles;texture.tiles_=&textureTiles;
      }
      material.map=&texture;
    }
  }
};

struct Renderer::RasterContext {
  const RasterBatch* batch=nullptr;
  RenderSurface surface;
  uint16_t* depth=nullptr;
  uint16_t firstRow=0,lastRow=0;
  uint8_t perspectiveSpan=8;
  Side triangleSide=Side::Front;
  bool sampleStack=false;
  bool measureTime=false;
  uint32_t triangles=0,clippedTriangles=0,drawnTriangles=0,shadedPixels=0;
};

struct Renderer::VertexContext {
  const Vertex* inputs=nullptr;
  const detail::StaticVertexReuse* reuse=nullptr;
  const SkinnedMesh* skinned=nullptr;
  ClipVertex* outputs=nullptr;
  size_t first=0,last=0;
  float m[16],normal[9],ambient[3],materialColor[3];
  LightData lights[4];
  size_t lightCount=0;
  bool lambert=false,deferSkinNormal=false,duplicates=false,measureTime=false;
  uint32_t reusedPositions=0,reusedLighting=0;
};

struct Renderer::RasterWorker {
#if NICE_GFX_3D_RASTER_TASKS
  static constexpr uint32_t StackBytes=16384;
  StackType_t* stack=nullptr;
  StaticTask_t taskStorage;
  StaticSemaphore_t completionStorage;
  TaskHandle_t task=nullptr;
  SemaphoreHandle_t completion=nullptr;
  union Job {
    RasterContext raster;
    VertexContext* vertices;
    Job():raster(){}
  } job;
  enum class Kind:uint8_t {Idle,Raster,Vertices};
  Status status=Status::NotInitialized;
  std::atomic<bool> ready{false},done{false};
  Kind kind=Kind::Idle;
  int8_t core=-1;
  uint32_t stackFreeBytes=0;
  uint64_t elapsedMicros=0;
#endif
};

void Renderer::prepareRasterWorker() {
#if NICE_GFX_3D_RASTER_TASKS
  if(options_.rasterWorkers!=2)return;
  constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT|MALLOC_CAP_DMA;
  void* memory=heap_caps_malloc(sizeof(RasterWorker),caps);
  if(!memory)return;
  auto* worker=new(memory) RasterWorker;
  worker->stack=static_cast<StackType_t*>(heap_caps_malloc(RasterWorker::StackBytes,caps));
  if(worker->stack)worker->completion=xSemaphoreCreateBinaryStatic(&worker->completionStorage);
  if(!worker->stack||!worker->completion){
    heap_caps_free(worker->stack);
    worker->~RasterWorker();heap_caps_free(worker);
    return;
  }
  rasterWorker_=worker;
#endif
}

void Renderer::stopRasterTask() {
#if NICE_GFX_3D_RASTER_TASKS
  if(!rasterWorker_||!rasterWorker_->task)return;
  auto& worker=*rasterWorker_;
  if(worker.kind!=RasterWorker::Kind::Idle)waitRasterWorker();
  // Follow ESP-IDF's task-delete-with-caps synchronization. A task can enter a
  // blocked list before it has switched off the CPU. Suspend it, then observe
  // that no core still runs it before deleting and reusing its static storage.
  vTaskSuspend(worker.task);
  for(BaseType_t core=0;core<configNUMBER_OF_CORES;++core){
    while(xTaskGetCurrentTaskHandleForCore(core)==worker.task)vTaskDelay(1);
  }
  vTaskDelete(worker.task);
  worker.task=nullptr;worker.core=-1;
#endif
}

void Renderer::releaseRasterWorker() {
#if NICE_GFX_3D_RASTER_TASKS
  if(!rasterWorker_)return;
  stopRasterTask();
  auto* worker=rasterWorker_;
  vSemaphoreDelete(worker->completion);
  heap_caps_free(worker->stack);
  worker->~RasterWorker();heap_caps_free(worker);
  rasterWorker_=nullptr;
#endif
}

void Renderer::rasterWorkerTask(void* argument) {
#if NICE_GFX_3D_RASTER_TASKS
  auto& worker=*static_cast<RasterWorker*>(argument);
  for(;;){
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    if(!worker.ready.load(std::memory_order_acquire))continue;
    const bool vertex=worker.kind==RasterWorker::Kind::Vertices;
    VertexContext* vertices=vertex?worker.job.vertices:nullptr;
    const bool measureTime=vertex?vertices->measureTime:worker.job.raster.measureTime;
    const uint64_t started=measureTime?detail::microseconds():0;
    worker.status=vertex?transformVerticesDispatch(*vertices):rasterizeBatch(worker.job.raster);
    worker.elapsedMicros=measureTime?detail::microseconds()-started:0;
    if(!vertex&&worker.job.raster.sampleStack)
      worker.stackFreeBytes=uint32_t(uxTaskGetStackHighWaterMark(nullptr));
    worker.ready.store(false,std::memory_order_relaxed);
    worker.done.store(true,std::memory_order_release);
    xSemaphoreGive(worker.completion);
  }
#else
  (void)argument;
#endif
}

bool Renderer::ensureRasterTask() {
#if NICE_GFX_3D_RASTER_TASKS
  if(!rasterWorker_)return false;
  auto& worker=*rasterWorker_;
  if(worker.kind!=RasterWorker::Kind::Idle)return false;
  const int8_t core=xPortGetCoreID()==0?1:0;
  if(worker.task&&worker.core!=core)stopRasterTask();
  if(!worker.task){
    worker.task=xTaskCreateStaticPinnedToCore(rasterWorkerTask,"gfx3d-raster",
      RasterWorker::StackBytes,&worker,1,worker.stack,&worker.taskStorage,core);
    if(!worker.task)return false;
    worker.core=core;
  }
  return true;
#else
  return false;
#endif
}

bool Renderer::startRasterWorker(const RasterContext& context) {
#if NICE_GFX_3D_RASTER_TASKS
  if(!ensureRasterTask())return false;
  auto& worker=*rasterWorker_;
  new(&worker.job.raster) RasterContext(context);
  worker.status=Status::NotInitialized;
  worker.done.store(false,std::memory_order_relaxed);worker.kind=RasterWorker::Kind::Raster;
  worker.ready.store(true,std::memory_order_release);
  xTaskNotifyGive(worker.task);
  return true;
#else
  (void)context;
  return false;
#endif
}

bool Renderer::startVertexWorker(VertexContext& context) {
#if NICE_GFX_3D_RASTER_TASKS
  if(!ensureRasterTask())return false;
  auto& worker=*rasterWorker_;
  worker.job.vertices=&context;worker.status=Status::NotInitialized;
  worker.done.store(false,std::memory_order_relaxed);worker.kind=RasterWorker::Kind::Vertices;
  worker.ready.store(true,std::memory_order_release);
  xTaskNotifyGive(worker.task);
  return true;
#else
  (void)context;
  return false;
#endif
}

Status Renderer::waitRasterWorker() {
#if NICE_GFX_3D_RASTER_TASKS
  if(!rasterWorker_||rasterWorker_->kind==RasterWorker::Kind::Idle)return Status::NotInitialized;
  auto& worker=*rasterWorker_;
  do{xSemaphoreTake(worker.completion,portMAX_DELAY);}
  while(!worker.done.load(std::memory_order_acquire));
  worker.kind=RasterWorker::Kind::Idle;
  if(!stats_.rasterWorkerStackFreeBytes||worker.stackFreeBytes<stats_.rasterWorkerStackFreeBytes)
    stats_.rasterWorkerStackFreeBytes=worker.stackFreeBytes;
  stats_.rasterOwnerCore=int8_t(xPortGetCoreID());stats_.rasterHelperCore=worker.core;
  return worker.status;
#else
  return Status::Unsupported;
#endif
}

Status Renderer::joinRasterWorker(RasterContext& context) {
  const Status status=waitRasterWorker();
#if NICE_GFX_3D_RASTER_TASKS
  if(rasterWorker_){context=rasterWorker_->job.raster;stats_.helperRasterMicros+=rasterWorker_->elapsedMicros;}
#else
  (void)context;
#endif
  return status;
}

Status Renderer::joinVertexWorker() {
  const Status status=waitRasterWorker();
#if NICE_GFX_3D_RASTER_TASKS
  if(rasterWorker_){stats_.helperVertexMicros+=rasterWorker_->elapsedMicros;rasterWorker_->job.vertices=nullptr;}
#endif
  return status;
}

bool Renderer::aliasesRasterWorker(uintptr_t first,uintptr_t last) const {
#if NICE_GFX_3D_RASTER_TASKS
  if(!rasterWorker_)return false;
  const uintptr_t worker=reinterpret_cast<uintptr_t>(rasterWorker_);
  const uintptr_t stack=reinterpret_cast<uintptr_t>(rasterWorker_->stack);
  return (first<worker+sizeof(RasterWorker)&&worker<last)||
    (first<stack+RasterWorker::StackBytes&&stack<last);
#else
  (void)first;(void)last;
  return false;
#endif
}

struct Renderer::AsyncClearState {
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3 && configSUPPORT_STATIC_ALLOCATION == 1
  StaticSemaphore_t storage;
  SemaphoreHandle_t completion=nullptr;
  struct Buffer {uint16_t* pixels=nullptr;size_t bytes=0;uint16_t color=0;} buffers[2];
  uint8_t pending=0;
  static bool IRAM_ATTR onDone(ppa_client_handle_t,ppa_event_data_t*,void* user){
    auto* state=static_cast<AsyncClearState*>(user);
    // Blocking operations on the same client retain null transaction data.
    if(!state)return false;
    BaseType_t higherPriorityTaskWoken=pdFALSE;
    xSemaphoreGiveFromISR(state->completion,&higherPriorityTaskWoken);
    return higherPriorityTaskWoken==pdTRUE;
  }
#endif
};

bool Renderer::submitAsyncClear(uint16_t* pixels,uint16_t color){
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3 && configSUPPORT_STATIC_ALLOCATION == 1
  const size_t bytes=size_t(surface_.width)*surface_.height*2;
  auto* state=asyncClear_;
  if(!state||!state->completion||!clearPpa_||!pixels||state->pending>=2||
      (reinterpret_cast<uintptr_t>(pixels)&63)||(bytes&63))return false;
  ppa_fill_oper_config_t operation={};
  operation.out.buffer=pixels;operation.out.buffer_size=bytes;
  operation.out.pic_w=surface_.width;operation.out.pic_h=surface_.height;
  operation.out.fill_cm=PPA_FILL_COLOR_MODE_RGB565;
  operation.fill_block_w=surface_.width;operation.fill_block_h=surface_.height;
  const uint32_t argb=detail::ppaFillArgb8888FromRgb565(color);
  operation.fill_argb_color.a=0xff;operation.fill_argb_color.r=uint8_t(argb>>16);
  operation.fill_argb_color.g=uint8_t(argb>>8);operation.fill_argb_color.b=uint8_t(argb);
  operation.mode=PPA_TRANS_MODE_NON_BLOCKING;operation.user_data=state;
  // The installed driver retains its pre-DMA output C2M cache synchronization.
  // No CPU color/depth access follows until finishPendingClear joins every job.
  if(ppa_do_fill(static_cast<ppa_client_handle_t>(clearPpa_),&operation)!=ESP_OK)return false;
  state->buffers[state->pending++]={pixels,bytes,color};
  ++stats_.asyncClearFills;
  return true;
#else
  (void)pixels;(void)color;return false;
#endif
}

void Renderer::finishPendingClear(){
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3 && configSUPPORT_STATIC_ALLOCATION == 1
  auto* state=asyncClear_;
  if(!state||!state->pending)return;
  const uint64_t started=options_.measurePhases?detail::microseconds():0;
  const uint8_t count=state->pending;
  for(uint8_t i=0;i<count;++i)
    while(xSemaphoreTake(state->completion,portMAX_DELAY)!=pdTRUE){}
  state->pending=0;
  // DMA has finished for every submitted buffer. Invalidate possible CPU
  // prefetches before rasterization; only now is CPU fallback safe as well.
  for(uint8_t i=0;i<count;++i){
    const auto& buffer=state->buffers[i];
    if(!clearCacheSync(buffer.pixels,buffer.bytes,ESP_CACHE_MSYNC_FLAG_DIR_M2C)){
      fill565(buffer.pixels,buffer.bytes/2,buffer.color);
      ++stats_.clearCpuFills;++stats_.clearPpaFallbacks;++stats_.asyncClearFallbacks;
    }
  }
  if(options_.measurePhases){
    const uint64_t elapsed=detail::microseconds()-started;
    stats_.clearWaitMicros+=elapsed;stats_.clearMicros+=elapsed;
  }
#endif
}

Renderer::~Renderer() { end(); }
void Renderer::end() {
  finishPendingClear();
  releaseRasterWorker();
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3
  if(clearPpa_){
    // An ISR callback may wake the other core before the driver's final queue
    // bookkeeping. Keep the client/state alive until unregister confirms idle.
    esp_err_t result;
    do{
      result=ppa_unregister_client(static_cast<ppa_client_handle_t>(clearPpa_));
      if(result==ESP_ERR_INVALID_STATE)vTaskDelay(1);
    }while(result==ESP_ERR_INVALID_STATE);
  }
#if configSUPPORT_STATIC_ALLOCATION == 1
  if(asyncClear_){
    if(asyncClear_->completion)vSemaphoreDelete(asyncClear_->completion);
    asyncClear_->~AsyncClearState();heap_caps_free(asyncClear_);
  }
#endif
  asyncClear_=nullptr;
#endif
  clearPpa_=nullptr;
  detail::release(surface_.pixels); detail::release(depth_);
  detail::release(vertices_); detail::release(draws_); detail::release(stack_);
  surface_ = {}; depth_ = nullptr; vertices_ = nullptr; draws_ = nullptr; stack_ = nullptr;
  drawCount_ = 0; stats_ = {};
}
Status Renderer::begin(const RendererOptions &options) {
  if (!options.width || !options.height || options.width > 4096 || options.height > 4096 ||
      !options.maxVertices || !options.maxObjects || options.maxObjects > 65535 ||
      options.maxVertices > std::numeric_limits<size_t>::max() / sizeof(ClipVertex) ||
      options.rasterWorkers<1||options.rasterWorkers>2||
      options.perspectiveSpan == 0 || options.perspectiveSpan > 32) return Status::InvalidArgument;
  const size_t bytes = size_t(options.width) * options.height * sizeof(uint16_t);
  // Build temporary allocations before replacing a working renderer.
  auto *color = static_cast<uint16_t *>(detail::allocate(bytes, options.colorMemory,64));
  auto *depth = options.depthTest ? static_cast<uint16_t *>(detail::allocate(bytes, options.depthMemory,64)) : nullptr;
  auto *vertices = static_cast<ClipVertex *>(detail::allocate(size_t(options.maxVertices)*sizeof(ClipVertex), options.scratchMemory));
  auto *draws = static_cast<DrawItem *>(detail::allocate(size_t(options.maxObjects)*sizeof(DrawItem), options.scratchMemory));
  auto *stack = static_cast<Object3D **>(detail::allocate(size_t(options.maxObjects)*sizeof(Object3D *), options.scratchMemory));
  if (!color || (options.depthTest && !depth) || !vertices || !draws || !stack) {
    detail::release(color); detail::release(depth); detail::release(vertices); detail::release(draws); detail::release(stack);
    return Status::NoMemory;
  }
  for(size_t i=0;i<options.maxVertices;++i)new(vertices+i) ClipVertex;
  for(size_t i=0;i<options.maxObjects;++i)new(draws+i) DrawItem;
  end(); options_ = options;
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3
  ppa_client_config_t clearConfig={};clearConfig.oper_type=PPA_OPERATION_FILL;
  clearConfig.max_pending_trans_num=2;
  ppa_client_handle_t clearClient=nullptr;
  if(ppa_register_client(&clearConfig,&clearClient)==ESP_OK){
    clearPpa_=clearClient;
#if configSUPPORT_STATIC_ALLOCATION == 1
    void* memory=heap_caps_malloc(sizeof(AsyncClearState),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(memory){
      auto* state=new(memory) AsyncClearState;
      state->completion=xSemaphoreCreateCountingStatic(2,0,&state->storage);
      ppa_event_callbacks_t callbacks={};callbacks.on_trans_done=AsyncClearState::onDone;
      if(state->completion&&ppa_client_register_event_callbacks(clearClient,&callbacks)==ESP_OK)asyncClear_=state;
      else{
        if(state->completion)vSemaphoreDelete(state->completion);
        state->~AsyncClearState();heap_caps_free(state);
      }
    }
#endif
  }
#endif
  surface_ = {color, options.width, options.height, options.width};
  depth_ = depth; vertices_ = vertices; draws_ = draws; stack_ = stack;
  rasterSplitRow_=options.height/2;
  prepareRasterWorker();
  stats_.bufferBytes = bytes * (depth ? 2 : 1);
  stats_.scratchBytes = size_t(options.maxVertices)*sizeof(ClipVertex) +
      size_t(options.maxObjects)*(sizeof(DrawItem)+sizeof(Object3D *));
  fill565(color, size_t(options.width)*options.height, options.clearColor);
  return Status::Ok;
}

Status Renderer::updateWorlds(Object3D &root,bool descendants) {
  // Bounded ancestor walk and breadth-first world update avoid using the MCU
  // task stack for imported scene hierarchies. Public scene APIs remain useful
  // independently, but render does not recurse through caller-supplied nodes.
  size_t count=0;
  for(Object3D *p=&root;p;p=p->parent) {
    if(count>=options_.maxObjects)return Status::CapacityExceeded;
    stack_[count++]=p;
  }
  while(count)stack_[--count]->updateWorldMatrix(false,false);
  if(!descendants)return Status::Ok;
  stack_[0]=&root;count=1;
  for(size_t at=0;at<count;++at) {
    Object3D *object=stack_[at];
    for(Object3D *child:object->children)if(child) {
      if(child->parent!=object)return Status::InvalidData;
      if(count>=options_.maxObjects)return Status::CapacityExceeded;
      child->updateWorldMatrix(false,false);stack_[count++]=child;
    }
  }
  return Status::Ok;
}

Status Renderer::collect(Object3D &root, Camera &camera) {
  size_t count = 0; stack_[count++] = &root;
  drawCount_ = 0; lightCount_ = 0;
  ambient_[0] = ambient_[1] = ambient_[2] = 0;
  const auto &view = camera.matrixWorldInverse;
  while (count) {
    Object3D *object = stack_[--count];
    if (++stats_.objects > options_.maxObjects) return Status::CapacityExceeded;
    if (!object->visible) continue;
    for (Object3D *child : object->children) {
      if (!child) continue;
      if (count >= options_.maxObjects) return Status::CapacityExceeded;
      stack_[count++] = child;
    }
    if (object->type() == ObjectType::AmbientLight) {
      auto &light = static_cast<AmbientLight &>(*object);
      float r,g,b; unpack(light.color,r,g,b);
      ambient_[0] += r*light.intensity; ambient_[1] += g*light.intensity; ambient_[2] += b*light.intensity;
    } else if (object->type() == ObjectType::DirectionalLight && lightCount_ < 4) {
      auto &light = static_cast<DirectionalLight &>(*object);
      auto &out = lights_[lightCount_++];
      const float *m = light.matrixWorld.elements;
      const auto &d = light.direction;
      out.direction.set(-(m[0]*d.x+m[4]*d.y+m[8]*d.z),
                        -(m[1]*d.x+m[5]*d.y+m[9]*d.z),
                        -(m[2]*d.x+m[6]*d.y+m[10]*d.z)).normalize();
      unpack(light.color,out.r,out.g,out.b);
      out.r *= light.intensity; out.g *= light.intensity; out.b *= light.intensity;
    } else if (object->type() == ObjectType::Mesh || object->type() == ObjectType::SkinnedMesh) {
      auto &mesh = static_cast<Mesh &>(*object);
      if (!mesh.geometry || !mesh.material || !mesh.material->visible) continue;
      if (object->type()!=ObjectType::SkinnedMesh && mesh.geometry->boundingSphere.radius>=0) {
        Vector3 bound=mesh.geometry->boundingSphere.center;
        bound.applyMatrix4(mesh.matrixWorld);
        const float *w=mesh.matrixWorld.elements;
        // Frobenius bound remains conservative even with inherited shear.
        const float scale=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]+w[4]*w[4]+w[5]*w[5]+w[6]*w[6]+w[8]*w[8]+w[9]*w[9]+w[10]*w[10]);
        if(!sphereVisible(viewProjection_,bound,mesh.geometry->boundingSphere.radius*scale)) {++stats_.culledMeshes;continue;}
      }
      if (drawCount_ >= options_.maxObjects) return Status::CapacityExceeded;
      Vector3 center(mesh.matrixWorld.elements[12],mesh.matrixWorld.elements[13],mesh.matrixWorld.elements[14]);
      center.applyMatrix4(view);
      if(!center.isFinite())return Status::InvalidData;
      draws_[drawCount_] = {&mesh,-center.z,drawCount_}; ++drawCount_;
    }
  }
  // Opaque front-to-back for early Z, transparent back-to-front. No sort buffer.
  std::sort(draws_,draws_+drawCount_,[](const DrawItem &a,const DrawItem &b) {
    const bool at=transparent(*a.mesh->material),bt=transparent(*b.mesh->material);
    if (at!=bt) return !at;
    if (a.depth!=b.depth) return at ? a.depth>b.depth : a.depth<b.depth;
    return a.order<b.order;
  });
  return Status::Ok;
}

Status Renderer::renderToSurface(Object3D &scene,Camera &camera,const RenderSurface &target) {
  if(!ready())return Status::NotInitialized;
  if(!target.pixels||target.width!=options_.width||target.height!=options_.height||
     target.stride!=target.width||reinterpret_cast<uintptr_t>(target.pixels)%alignof(uint16_t))
    return Status::InvalidArgument;
  const size_t bytes=size_t(target.width)*target.height*sizeof(uint16_t);
  const uintptr_t first=reinterpret_cast<uintptr_t>(target.pixels);
  if(first>std::numeric_limits<uintptr_t>::max()-bytes)return Status::InvalidArgument;
  const uintptr_t last=first+bytes;
  const auto aliases=[&](const void *memory,size_t capacity) {
    if(!memory)return false;
    const uintptr_t start=reinterpret_cast<uintptr_t>(memory);
    if(start>std::numeric_limits<uintptr_t>::max()-capacity)return true;
    return first<start+capacity&&start<last;
  };
  if((target.pixels!=surface_.pixels&&aliases(surface_.pixels,
       size_t(surface_.stride)*surface_.height*sizeof(uint16_t)))||
     aliases(depth_,bytes)||
     aliases(vertices_,size_t(options_.maxVertices)*sizeof(ClipVertex))||
     aliases(draws_,size_t(options_.maxObjects)*sizeof(DrawItem))||
     aliases(stack_,size_t(options_.maxObjects)*sizeof(Object3D *))||
     aliases(asyncClear_,sizeof(AsyncClearState))||
     aliasesRasterWorker(first,last))return Status::InvalidArgument;
  struct RestoreSurface {
    RenderSurface &active;
    RenderSurface owned;
    ~RestoreSurface(){active=owned;}
  } restore{surface_,surface_};
  surface_=target;
  return render(scene,camera);
}
Status Renderer::render(Object3D &scene, Camera &camera) {
  if (!ready()) return Status::NotInitialized;
  const uint64_t start = detail::microseconds();
  const size_t bufferBytes = stats_.bufferBytes, scratchBytes = stats_.scratchBytes;
  stats_ = {}; stats_.bufferBytes = bufferBytes; stats_.scratchBytes = scratchBytes;
  stats_.rasterSplitRow=rasterSplitRow_;
  struct ClearOnExit {
    Renderer& renderer;
    ~ClearOnExit(){renderer.finishPendingClear();}
  } clearOnExit{*this};
  const uint64_t worldAt = options_.measurePhases ? detail::microseconds() : 0;
  Status result=updateWorlds(scene,true);
  if(result!=Status::Ok)return result;
  result=updateWorlds(camera,false);
  if(result!=Status::Ok)return result;
  camera.matrixWorldInverse=camera.matrixWorld;
  if(!camera.matrixWorldInverse.tryInvert())return Status::InvalidArgument;
  viewProjection_.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse);
  for(float value:viewProjection_.elements)if(!std::isfinite(value))return Status::InvalidArgument;
  result = collect(scene,camera);
  if (result != Status::Ok) return result;
  if(options_.measurePhases)stats_.worldMicros=detail::microseconds()-worldAt;
  const uint64_t clearAt=options_.measurePhases?detail::microseconds():0;
  bool colorCleared=false,depthCleared=false;
#if NICE_GFX_3D_ESP && NICE_GFX_3D_CLEAR_METHOD == 3
  if(options_.asyncClear){
    const uint64_t submitted=options_.measurePhases?detail::microseconds():0;
    colorCleared=submitAsyncClear(surface_.pixels,options_.clearColor);
    if(depth_)depthCleared=submitAsyncClear(depth_,0xffff);
    if(options_.measurePhases)stats_.clearSubmitMicros=detail::microseconds()-submitted;
    stats_.asyncClearFallbacks+=uint32_t(!colorCleared)+uint32_t(depth_&&!depthCleared);
    if(!colorCleared||(depth_&&!depthCleared))finishPendingClear();
  }
  // A failed async submission has drained all previously queued DMA before
  // the ordinary blocking-PPA/CPU paths can touch either target.
  if(!colorCleared)colorCleared=fill565Ppa(clearPpa_,surface_.pixels,surface_.width,surface_.height,options_.clearColor);
  if(depth_&&!depthCleared)depthCleared=fill565Ppa(clearPpa_,depth_,surface_.width,surface_.height,0xffff);
  stats_.clearPpaFills=uint32_t(colorCleared)+uint32_t(depthCleared);
  stats_.clearPpaFallbacks+=uint32_t(!colorCleared)+uint32_t(depth_&&!depthCleared);
#endif
  if(!colorCleared){fill565(surface_.pixels,size_t(surface_.width)*surface_.height,options_.clearColor);++stats_.clearCpuFills;}
  if(depth_&&!depthCleared){
#if NICE_GFX_3D_CLEAR_METHOD == 0
    std::memset(depth_,0xff,size_t(surface_.width)*surface_.height*2);
#else
    fill565(depth_,size_t(surface_.width)*surface_.height,0xffff);
#endif
    ++stats_.clearCpuFills;
  }
  if(options_.measurePhases)stats_.clearMicros=detail::microseconds()-clearAt;
  for (size_t i=0;i<drawCount_;++i) {
    result=drawMesh(draws_[i],camera);
    if (result!=Status::Ok) break;
  }
#if NICE_GFX_3D_ESP
  if(options_.balanceRasterWorkers&&surface_.height>=4&&stats_.rasterWorkerJobs&&!stats_.rasterWorkerFallbacks){
    const float owner=float(stats_.ownerRasterMicros),helper=float(stats_.helperRasterMicros);
    const float sum=owner+helper,difference=owner-helper;
    if(sum>1000&&std::abs(difference)>500){
      int step=int(difference/sum*float(surface_.height)*0.125f);
      if(!step)step=difference>0?1:-1;
      step=std::max(-8,std::min(8,step));
      const int lower=std::max(1,int(surface_.height)/4),upper=int(surface_.height)-lower;
      rasterSplitRow_=uint16_t(std::max(lower,std::min(upper,int(rasterSplitRow_)-step)));
    }
  }
#endif
  finishPendingClear();
  stats_.renderMicros=detail::microseconds()-start;
  return result;
}

namespace {
NICE_GFX_3D_HOT_INLINE Vertex originalVertex(const detail::StaticVertexReuse* reuse,const Vertex* vertices,size_t index){
  if(!reuse||!reuse->originalPositions)return vertices[index];
  const auto& position=reuse->originalPositions[reuse->positions[index]>>16];
  const auto& normalColor=reuse->originalNormalColors[reuse->lighting[index]>>16];
  return Vertex{position,normalColor.normal,reuse->uvs[index],normalColor.color};
}
}
Status Renderer::transformVertices(VertexContext& context) {
  const float* m=context.m;
  const float* normal=context.normal;
  for(size_t i=context.first;i<context.last;++i){
    const size_t positionSource=context.reuse?(context.reuse->positions[i]&context.reuse->sourceMask):i;
    const size_t lightingSource=context.reuse?(context.reuse->lighting[i]&context.reuse->sourceMask):i;
    const bool reusePosition=positionSource<i,reuseLighting=lightingSource<i;
    auto& out=context.outputs[i];
    if(context.duplicates){
      const auto& uv=context.reuse?context.reuse->uvs[i]:context.inputs[i].uv;
      if(!std::isfinite(uv.x)||!std::isfinite(uv.y)||
         std::abs(uv.x)>1e12f||std::abs(uv.y)>1e12f)return Status::InvalidData;
      if(reusePosition){
        const auto& previous=context.outputs[positionSource];
        out.x=previous.x;out.y=previous.y;out.z=previous.z;out.w=previous.w;
        out.inverseW=previous.inverseW;out.clipMask=previous.clipMask;
        ++context.reusedPositions;
      }
      if(reuseLighting){
        const auto& previous=context.outputs[lightingSource];
        out.r=previous.r;out.g=previous.g;out.b=previous.b;
        ++context.reusedLighting;
      }
      out.u=uv.x;out.v=uv.y;
      continue;
    }
    // Both maps point directly to their first canonical input. The first phase
    // never reads an output representative, even when it is on this task's side.
    if(reusePosition&&reuseLighting)continue;
    Vertex vertex=originalVertex(context.reuse,context.inputs,i);
    const uint16_t cachedJoint=context.reuse&&context.reuse->rigidJoints?context.reuse->rigidJoints[i]:UINT16_MAX;
    if(context.skinned){
      if(!reusePosition&&!reuseLighting)vertex=context.deferSkinNormal?context.skinned->skinVertexImpl<false>(i,vertex,cachedJoint):context.skinned->skinVertexImpl<true>(i,vertex,cachedJoint);
      else if(!reusePosition)vertex=context.skinned->skinVertexImpl<false,true,false>(i,vertex,cachedJoint);
      else if(!reuseLighting)vertex=context.deferSkinNormal?context.skinned->skinVertexImpl<false,false,true>(i,vertex,cachedJoint):context.skinned->skinVertexImpl<true,false,true>(i,vertex,cachedJoint);
    }
    if(context.deferSkinNormal&&!reuseLighting){
      const float length2=vertex.normal.lengthSq();
      if(!std::isfinite(length2)||length2<1e-12f||length2>1e12f)vertex.normal.normalize();
    }
    if(!finite3(vertex.position)||!finite3(vertex.normal)||
       !std::isfinite(vertex.uv.x)||!std::isfinite(vertex.uv.y)||
       std::abs(vertex.uv.x)>1e12f||std::abs(vertex.uv.y)>1e12f)return Status::InvalidData;
    if(!reusePosition){
      const auto& p=vertex.position;
      out.x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12];
      out.y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13];
      out.z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];
      out.w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
      if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||!std::isfinite(out.w)||
         std::abs(out.x)>1e30f||std::abs(out.y)>1e30f||std::abs(out.z)>1e30f||std::abs(out.w)>1e30f)return Status::InvalidData;
      out.inverseW=out.w>1e-9f?1.0f/out.w:0.0f;
      out.clipMask=0;
      if(out.w+out.x<0)out.clipMask|=1u;if(out.w-out.x<0)out.clipMask|=2u;
      if(out.w+out.y<0)out.clipMask|=4u;if(out.w-out.y<0)out.clipMask|=8u;
      if(out.w+out.z<0)out.clipMask|=16u;if(out.w-out.z<0)out.clipMask|=32u;
    }
    if(reuseLighting)continue;
    unpack(vertex.color,out.r,out.g,out.b);
    out.r*=context.materialColor[0];out.g*=context.materialColor[1];out.b*=context.materialColor[2];
    if(context.lambert){
      const auto& n=vertex.normal;
      Vector3 wn(normal[0]*n.x+normal[1]*n.y+normal[2]*n.z,
                 normal[3]*n.x+normal[4]*n.y+normal[5]*n.z,
                 normal[6]*n.x+normal[7]*n.y+normal[8]*n.z);
      wn.normalize();
      float r=context.ambient[0],g=context.ambient[1],b=context.ambient[2];
      for(size_t l=0;l<context.lightCount;++l){
        const float diffuse=std::max(0.0f,wn.dot(context.lights[l].direction));
        r+=diffuse*context.lights[l].r;g+=diffuse*context.lights[l].g;b+=diffuse*context.lights[l].b;
      }
      out.r*=clamp01(r);out.g*=clamp01(g);out.b*=clamp01(b);
    }
  }
  return Status::Ok;
}

// Context layout and the ordinary transformVertices body remain untouched.
// deferSkinNormal is already false for Basic. A mesh-proven Basic context uses
// it as a private "unused normal is safe" marker; Lambert keeps its old meaning.
Status Renderer::transformVerticesDispatch(VertexContext& context){
  if(!context.lambert&&context.deferSkinNormal)return transformVerticesBasicConstant(context);
  return transformVertices(context);
}
Status Renderer::transformVerticesBasicConstant(VertexContext& context){
  const auto& reuse=*context.reuse;const float* m=context.m;
  for(size_t i=context.first;i<context.last;++i){
    const size_t positionSource=reuse.positions[i]&reuse.sourceMask;
    const bool reusePosition=positionSource<i;auto& out=context.outputs[i];
    if(context.duplicates){
      const auto& uv=reuse.uvs[i];
      if(!std::isfinite(uv.x)||!std::isfinite(uv.y)||std::abs(uv.x)>1e12f||std::abs(uv.y)>1e12f)return Status::InvalidData;
      if(reusePosition){const auto& p=context.outputs[positionSource];out.x=p.x;out.y=p.y;out.z=p.z;out.w=p.w;out.inverseW=p.inverseW;out.clipMask=p.clipMask;++context.reusedPositions;}
      if(i){const auto& color=context.outputs[0];out.r=color.r;out.g=color.g;out.b=color.b;++context.reusedLighting;}
      out.u=uv.x;out.v=uv.y;continue;
    }
    const uint16_t joint=reuse.rigidJoints[i];const bool nonRigid=joint==UINT16_MAX;
    Vertex fallback;
    if(nonRigid){
      // Retain full filtering, overflow detection and normalization for blended
      // or unusual weights. Gundam has64 such inputs; no rigid approximation.
      fallback=originalVertex(&reuse,context.inputs,i);
      fallback=reusePosition?context.skinned->skinVertexImpl<true,false,true>(i,fallback):context.skinned->skinVertexImpl<true>(i,fallback);
      if(!finite3(fallback.position)||!finite3(fallback.normal)||!std::isfinite(fallback.uv.x)||!std::isfinite(fallback.uv.y))return Status::InvalidData;
    }
    if(reusePosition&&i)continue;
    const auto& uv=reuse.uvs[i];
    if(!std::isfinite(uv.x)||!std::isfinite(uv.y)||std::abs(uv.x)>1e12f||std::abs(uv.y)>1e12f)return Status::InvalidData;
    if(!reusePosition){
      Vertex vertex=nonRigid?fallback:Vertex{reuse.originalPositions[reuse.positions[i]>>16],Vector3{},uv,reuse.constantVertexColor};
      // The rigid position path retains exact +0,p*1 and normalization order.
      if(!nonRigid)vertex=context.skinned->skinVertexImpl<false,true,false>(i,vertex,joint);
      if(!finite3(vertex.position))return Status::InvalidData;const auto& p=vertex.position;
      out.x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12];out.y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13];
      out.z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];out.w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
      if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||!std::isfinite(out.w)||std::abs(out.x)>1e30f||std::abs(out.y)>1e30f||std::abs(out.z)>1e30f||std::abs(out.w)>1e30f)return Status::InvalidData;
      out.inverseW=out.w>1e-9f?1.0f/out.w:0.0f;out.clipMask=0;
      if(out.w+out.x<0)out.clipMask|=1u;if(out.w-out.x<0)out.clipMask|=2u;
      if(out.w+out.y<0)out.clipMask|=4u;if(out.w-out.y<0)out.clipMask|=8u;
      if(out.w+out.z<0)out.clipMask|=16u;if(out.w-out.z<0)out.clipMask|=32u;
    }
    if(!i){unpack(reuse.constantVertexColor,out.r,out.g,out.b);out.r*=context.materialColor[0];out.g*=context.materialColor[1];out.b*=context.materialColor[2];}
  }
  return Status::Ok;
}

Status Renderer::drawMesh(const DrawItem &item, Camera &camera) {
  Mesh &mesh=*item.mesh;
  const BufferGeometry &geometry=*mesh.geometry;
  const Material &material=*mesh.material;
  if (!geometry.vertexCount || !geometry.vertices || material.opacity<=0) return Status::Ok;
  if (geometry.vertexCount>options_.maxVertices) return Status::CapacityExceeded;
  const size_t indexCount=geometry.indices ? geometry.indexCount : geometry.vertexCount;
  if (indexCount%3 || !std::isfinite(material.opacity) || !std::isfinite(material.alphaTest)) return Status::InvalidData;
  if (material.map && (!material.map->pixels || !material.map->width || !material.map->height ||
      material.map->stride<material.map->width ||
      size_t(material.map->height)>SIZE_MAX/sizeof(uint16_t)/material.map->stride)) return Status::InvalidData;
  Matrix4 mvp; mvp.multiplyMatrices(viewProjection_,mesh.matrixWorld);
  const float *m=mvp.elements, *world=mesh.matrixWorld.elements;
  const float determinant=determinant3(mesh.matrixWorld);
  if (!std::isfinite(determinant) || std::abs(determinant)<1e-12f) return Status::Ok;
  // Inverse-transpose 3x3 correctly handles non-uniformly scaled models.
  const float inv=1.0f/determinant;
  const float normal[9]={
    (world[5]*world[10]-world[9]*world[6])*inv,
    (world[9]*world[2]-world[1]*world[10])*inv,
    (world[1]*world[6]-world[5]*world[2])*inv,
    (world[8]*world[6]-world[4]*world[10])*inv,
    (world[0]*world[10]-world[8]*world[2])*inv,
    (world[4]*world[2]-world[0]*world[6])*inv,
    (world[4]*world[9]-world[8]*world[5])*inv,
    (world[8]*world[1]-world[0]*world[9])*inv,
    (world[0]*world[5]-world[4]*world[1])*inv};
  SkinnedMesh *skinned=mesh.type()==ObjectType::SkinnedMesh ? static_cast<SkinnedMesh *>(&mesh) : nullptr;
  const uint64_t skinAt=options_.measurePhases?detail::microseconds():0;
  if (skinned && skinned->skeleton && !skinned->prepareSkinning()) return Status::InvalidData;
  if(options_.measurePhases)stats_.skinMicros+=detail::microseconds()-skinAt;
  bool deferSkinNormal=skinned&&skinned->skeleton&&material.type==MaterialType::Lambert;
  for(float value:normal)if(!std::isfinite(value)||std::fabs(value)>1e12f)deferSkinNormal=false;
  const uint64_t vertexAt=options_.measurePhases?detail::microseconds():0;
  float mr,mg,mb; unpack(material.color,mr,mg,mb);
  const auto* reuse=geometry.staticReuse_;
  if(reuse&&(reuse->vertices!=geometry.vertices||reuse->skinVertices!=geometry.skinVertices||reuse->vertexCount!=geometry.vertexCount))reuse=nullptr;
  bool basicConstant=false;
  if(material.type==MaterialType::Basic&&options_.parallelVertexWorkers&&options_.rasterWorkers==2&&geometry.vertexCount>=128&&
      reuse&&reuse->originalPositions&&reuse->originalNormalColors&&reuse->rigidJoints&&reuse->constantColorValid&&reuse->boundedRigidNormals&&
      skinned&&skinned->skeleton&&skinned->paletteReady_&&reuse->maximumRigidJoint<skinned->normalMatrices_.size()){
    basicConstant=true;
    // Each raw normal component <=1e12 and rigid weight is exactly1. Choose
    // a palette bound with each product <=1e30, so a component stays <=3e30,
    // safely below FLT_MAX. Existing normalize leaves finite components alone
    // if lengthSq overflows. Nonrigid vertices retain their original path.
    const float paletteLimit=reuse->maximumBasicNormal>1?1e30f/reuse->maximumBasicNormal:1e30f;
    for(const auto& matrix:skinned->normalMatrices_){const auto* e=matrix.elements;
      for(unsigned column=0;column<3&&basicConstant;++column)for(unsigned row=0;row<3;++row)
        if(!std::isfinite(e[column*4+row])||std::fabs(e[column*4+row])>paletteLimit){basicConstant=false;break;}
      if(!basicConstant)break;
    }
  }
  bool parallelVertices=false;
  if(options_.parallelVertexWorkers&&options_.rasterWorkers==2&&geometry.vertexCount>1){
    VertexContext upper;
    upper.inputs=geometry.vertices;upper.outputs=vertices_;upper.reuse=reuse;upper.skinned=skinned;
    upper.last=geometry.vertexCount/2;upper.deferSkinNormal=deferSkinNormal||basicConstant;
    upper.lambert=material.type==MaterialType::Lambert;upper.lightCount=lightCount_;
    upper.measureTime=options_.measurePhases;
    std::copy(m,m+16,upper.m);std::copy(normal,normal+9,upper.normal);
    std::copy(ambient_,ambient_+3,upper.ambient);std::copy(lights_,lights_+lightCount_,upper.lights);
    upper.materialColor[0]=mr;upper.materialColor[1]=mg;upper.materialColor[2]=mb;
    VertexContext lower=upper;lower.first=upper.last;lower.last=geometry.vertexCount;
#if NICE_GFX_3D_ESP
    const bool helperStarted=startVertexWorker(lower);
#else
    const bool helperStarted=true;
#endif
    if(helperStarted){
      parallelVertices=true;if(basicConstant)++stats_.basicConstantMeshes;
      for(unsigned phase=0;phase<2;++phase){
        bool helperRunning=true;
        if(phase){
          upper.duplicates=true;lower.duplicates=true;
#if NICE_GFX_3D_ESP
          helperRunning=startVertexWorker(lower);
#endif
        }
        const uint64_t ownerAt=options_.measurePhases?detail::microseconds():0;
        const Status ownerStatus=transformVerticesDispatch(upper);
        if(options_.measurePhases)stats_.ownerVertexMicros+=detail::microseconds()-ownerAt;
        const uint64_t waitAt=options_.measurePhases?detail::microseconds():0;
        Status helperStatus;
#if NICE_GFX_3D_ESP
        if(helperRunning)helperStatus=joinVertexWorker();
        else{helperStatus=transformVerticesDispatch(lower);++stats_.vertexWorkerFallbacks;}
#else
        helperStatus=transformVerticesDispatch(lower);
#endif
        if(options_.measurePhases)stats_.vertexJoinWaitMicros+=detail::microseconds()-waitAt;
        if(helperRunning)++stats_.vertexWorkerJobs;
        if(ownerStatus!=Status::Ok)return ownerStatus;
        if(helperStatus!=Status::Ok)return helperStatus;
      }
      stats_.reusedPositions+=upper.reusedPositions+lower.reusedPositions;
      stats_.reusedLighting+=upper.reusedLighting+lower.reusedLighting;
    }else ++stats_.vertexWorkerFallbacks;
  }
  if(!parallelVertices){
  for (size_t i=0;i<geometry.vertexCount;++i) {
    const size_t positionSource=reuse?(reuse->positions[i]&reuse->sourceMask):i;
    const size_t lightingSource=reuse?(reuse->lighting[i]&reuse->sourceMask):i;
    const bool reusePosition=positionSource<i,reuseLighting=lightingSource<i;
    if(reusePosition&&reuseLighting){
      // Both representatives have already passed this mesh's validation and
      // transforms in this frame. UVs are independent of both immutable maps.
      const auto &uv=reuse->uvs[i];
      if(!std::isfinite(uv.x)||!std::isfinite(uv.y)||
         std::abs(uv.x)>1e12f||std::abs(uv.y)>1e12f)return Status::InvalidData;
      const auto &position=vertices_[positionSource];
      const auto &lighting=vertices_[lightingSource];
      auto &out=vertices_[i];
      out.x=position.x;out.y=position.y;out.z=position.z;out.w=position.w;
      out.inverseW=position.inverseW;out.clipMask=position.clipMask;
      out.r=lighting.r;out.g=lighting.g;out.b=lighting.b;
      out.u=uv.x;out.v=uv.y;
      ++stats_.reusedPositions;++stats_.reusedLighting;
      continue;
    }
    Vertex vertex=originalVertex(reuse,geometry.vertices,i);
    const uint16_t cachedJoint=reuse&&reuse->rigidJoints?reuse->rigidJoints[i]:UINT16_MAX;
    if(skinned){
      if(!reusePosition&&!reuseLighting)vertex=deferSkinNormal?skinned->skinVertexImpl<false>(i,vertex,cachedJoint):skinned->skinVertexImpl<true>(i,vertex,cachedJoint);
      else if(!reusePosition)vertex=skinned->skinVertexImpl<false,true,false>(i,vertex,cachedJoint);
      else if(!reuseLighting)vertex=deferSkinNormal?skinned->skinVertexImpl<false,false,true>(i,vertex,cachedJoint):skinned->skinVertexImpl<true,false,true>(i,vertex,cachedJoint);
    }
    if(deferSkinNormal&&!reuseLighting){
      // Keep the established numerical path for extreme scale/normal inputs.
      // The ordinary finite range can defer the redundant positive scaling.
      const float length2=vertex.normal.lengthSq();
      if(!std::isfinite(length2)||length2<1e-12f||length2>1e12f)vertex.normal.normalize();
    }
    if (!finite3(vertex.position) || !finite3(vertex.normal) ||
        !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) return Status::InvalidData;
    auto &out=vertices_[i];
    const auto &p=vertex.position;
    if(reusePosition){
      const auto& previous=vertices_[positionSource];
      out.x=previous.x;out.y=previous.y;out.z=previous.z;out.w=previous.w;
      out.inverseW=previous.inverseW;out.clipMask=previous.clipMask;
      ++stats_.reusedPositions;
    }else{
      out.x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12];
      out.y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13];
      out.z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];
      out.w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
    }
    if (!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||!std::isfinite(out.w)||
        std::abs(out.x)>1e30f||std::abs(out.y)>1e30f||std::abs(out.z)>1e30f||std::abs(out.w)>1e30f||
        std::abs(vertex.uv.x)>1e12f||std::abs(vertex.uv.y)>1e12f) return Status::InvalidData;
    if(!reusePosition){
      out.inverseW=out.w>1e-9f?1.0f/out.w:0.0f;
      out.clipMask=0;
      if(out.w+out.x<0)out.clipMask|=1u;if(out.w-out.x<0)out.clipMask|=2u;
      if(out.w+out.y<0)out.clipMask|=4u;if(out.w-out.y<0)out.clipMask|=8u;
      if(out.w+out.z<0)out.clipMask|=16u;if(out.w-out.z<0)out.clipMask|=32u;
    }
    out.u=vertex.uv.x; out.v=vertex.uv.y;
    if(reuseLighting){
      const auto& previous=vertices_[lightingSource];
      out.r=previous.r;out.g=previous.g;out.b=previous.b;
      ++stats_.reusedLighting;
      continue;
    }
    unpack(vertex.color,out.r,out.g,out.b);
    out.r*=mr;out.g*=mg;out.b*=mb;
    if (material.type==MaterialType::Lambert) {
      const auto &n=vertex.normal;
      Vector3 wn(normal[0]*n.x+normal[1]*n.y+normal[2]*n.z,
                 normal[3]*n.x+normal[4]*n.y+normal[5]*n.z,
                 normal[6]*n.x+normal[7]*n.y+normal[8]*n.z);
      wn.normalize();
      float r=ambient_[0],g=ambient_[1],b=ambient_[2];
      for (size_t l=0;l<lightCount_;++l) {
        const float diffuse=std::max(0.0f,wn.dot(lights_[l].direction));
        r+=diffuse*lights_[l].r;g+=diffuse*lights_[l].g;b+=diffuse*lights_[l].b;
      }
      out.r*=clamp01(r);out.g*=clamp01(g);out.b*=clamp01(b);
    }
  }
  } // serial fallback
  ++stats_.meshes;stats_.vertices+=uint32_t(geometry.vertexCount);
  // Reuse maps above borrow the original clip coordinates and unweighted
  // colours until the last vertex has been transformed. Once a whole mesh is
  // inside the clip volume, preserve that storage as a read-only screen-space
  // cache for both raster contexts. Any clipped mesh keeps its original data.
  bool screenProjected=true;
  for(size_t i=0;i<geometry.vertexCount;++i){
    if(vertices_[i].clipMask){screenProjected=false;break;}
  }
  if(screenProjected){
    ++stats_.cachedProjectionMeshes;
    stats_.cachedProjectionVertices+=uint32_t(geometry.vertexCount);
    for(size_t i=0;i<geometry.vertexCount;++i){
      auto& vertex=vertices_[i];
      const float inverseW=vertex.inverseW;
      vertex.x=(vertex.x*inverseW+1)*surface_.width*0.5f;
      vertex.y=(1-vertex.y*inverseW)*surface_.height*0.5f;
      vertex.z=vertex.z*inverseW;
      vertex.u=vertex.u*inverseW;vertex.v=vertex.v*inverseW;
    }
  }
  if(options_.measurePhases)stats_.vertexMicros+=detail::microseconds()-vertexAt;
  finishPendingClear();
  const uint64_t triangleAt=options_.measurePhases?detail::microseconds():0;
  RasterBatch batch(vertices_,geometry,material,determinant<0,screenProjected);
  const auto* closed=geometry.closedSurfaces_;
  if(closed&&closed->hints&&closed->vertices==geometry.vertices&&closed->skinVertices==geometry.skinVertices&&
      closed->indices==geometry.indices&&closed->vertexCount==geometry.vertexCount&&closed->indexCount==geometry.indexCount&&
      material.side==Side::Double&&!transparent(material)&&material.opacity>=1.0f&&material.alphaTest<=0&&(!material.map||!material.map->alpha)){
    const auto& hints=*closed->hints;
    const auto* vp=viewProjection_.elements;
    const Vector3 plane(vp[3]+vp[2],vp[7]+vp[6],vp[11]+vp[10]);
    const float planeOffset=vp[15]+vp[14],planeLength=plane.length();
    const auto* cameraWorld=camera.matrixWorld.elements;
    const Vector3 eye(cameraWorld[12],cameraWorld[13],cameraWorld[14]);
    bool any=false;
    for(size_t i=0;i<hints.componentCount;++i){
      batch.closedSides[i]=Side::Double;
      const auto& component=hints.components[i];
      Matrix4 transform=mesh.matrixWorld;float skinDeterminant=1;
      bool valid=planeLength>0&&std::isfinite(planeLength)&&cameraWorld[3]==0&&cameraWorld[7]==0&&cameraWorld[11]==0&&cameraWorld[15]==1;
      if(component.joint!=UINT32_MAX){
        if(!skinned||!skinned->paletteReady_||component.joint>=skinned->skinMatrices_.size())valid=false;
        else{
          const auto& skin=skinned->skinMatrices_[component.joint];const auto* m=skin.elements;
          skinDeterminant=determinant3(skin);
          valid=valid&&std::isfinite(skinDeterminant)&&std::abs(skinDeterminant)>=1e-12f&&m[3]==0&&m[7]==0&&m[11]==0&&m[15]==1;
          if(valid)transform.multiply(skin);
        }
      }else if(skinned&&geometry.skinVertices)valid=false;
      const auto* t=transform.elements;
      valid=valid&&t[3]==0&&t[7]==0&&t[11]==0&&t[15]==1;
      if(!valid){++stats_.closedSurfaceFallbacks;continue;}
      Vector3 center=component.center;center.applyMatrix4(transform);
      const auto& h=component.halfExtent;
      const Vector3 half(std::abs(t[0])*h.x+std::abs(t[4])*h.y+std::abs(t[8])*h.z,
                         std::abs(t[1])*h.x+std::abs(t[5])*h.y+std::abs(t[9])*h.z,
                         std::abs(t[2])*h.x+std::abs(t[6])*h.y+std::abs(t[10])*h.z);
      const float radius=half.length();
      const float coordinateError=(std::abs(center.x)+std::abs(center.y)+std::abs(center.z))*2e-6f;
      const float margin=std::max(1e-6f,std::max(radius*1e-4f,coordinateError)),expanded=radius+margin;
      const float distance2=(eye-center).lengthSq(),nearDistance=plane.dot(center)+planeOffset;
      if(!center.isFinite()||!std::isfinite(radius)||!std::isfinite(distance2)||!std::isfinite(nearDistance)||
          !(distance2>expanded*expanded)||!(nearDistance>expanded*planeLength)){
        ++stats_.closedSurfaceFallbacks;continue;
      }
      batch.closedSides[i]=component.winding*skinDeterminant>0?Side::Front:Side::Back;
      ++stats_.closedSurfaceComponents;any=true;
    }
    if(any)batch.closedTriangleComponents=hints.triangleComponents;
  }

  if(batch.texture.tiles_){
    ++stats_.tiledTextureMeshes;if(batch.textureTiles.compactBlocks)++stats_.compactTextureMeshes;
  }
  RasterContext upper;
  upper.batch=&batch;upper.surface=surface_;upper.depth=depth_;
  upper.measureTime=options_.measurePhases||options_.balanceRasterWorkers;
  upper.perspectiveSpan=options_.perspectiveSpan;upper.lastRow=surface_.height;
  bool split=options_.rasterWorkers==2&&surface_.height>1;
  RasterContext lower=upper;
  bool helperRunning=false;
  if(split){
    upper.lastRow=options_.balanceRasterWorkers?rasterSplitRow_:surface_.height/2;
    lower.firstRow=upper.lastRow;
    // One stack scan per frame avoids rescanning 16 KiB for every mesh. The
    // next frame's first job observes the preceding frame's deepest call.
    lower.sampleStack=stats_.rasterWorkerJobs==0;
#if NICE_GFX_3D_ESP
    helperRunning=startRasterWorker(lower);
    if(!helperRunning){
      split=false;upper.lastRow=surface_.height;
      ++stats_.rasterWorkerFallbacks;
    }
#endif
  }
  const uint64_t ownerRasterAt=upper.measureTime?detail::microseconds():0;
  const Status ownerStatus=rasterizeBatch(upper);
  if(upper.measureTime)stats_.ownerRasterMicros+=detail::microseconds()-ownerRasterAt;
  Status helperStatus=Status::Ok;
  if(split){
    const uint64_t waitAt=options_.measurePhases?detail::microseconds():0;
    helperStatus=helperRunning?joinRasterWorker(lower):rasterizeBatch(lower);
    if(options_.measurePhases)stats_.rasterJoinWaitMicros+=detail::microseconds()-waitAt;
    stats_.rasterWorkersUsed=2;++stats_.rasterWorkerJobs;
  }
  // Clipping and triangle setup are the same in both contexts. Count geometry
  // once, and sum only the pixels written to their exclusive row ranges.
  stats_.triangles+=upper.triangles;
  stats_.clippedTriangles+=upper.clippedTriangles;
  stats_.drawnTriangles+=upper.drawnTriangles;
  stats_.shadedPixels+=upper.shadedPixels+(split?lower.shadedPixels:0);
  if(options_.measurePhases)stats_.triangleMicros+=detail::microseconds()-triangleAt;
  return ownerStatus==Status::Ok?helperStatus:ownerStatus;
}

Status Renderer::rasterizeBatch(RasterContext& context) {
  const auto& batch=*context.batch;
  for(size_t i=0;i<batch.indexCount;i+=3){
    const uint32_t a=batch.indices?batch.indices[i]:uint32_t(i);
    const uint32_t b=batch.indices?batch.indices[i+1]:uint32_t(i+1);
    const uint32_t c=batch.indices?batch.indices[i+2]:uint32_t(i+2);
    if(a>=batch.vertexCount||b>=batch.vertexCount||c>=batch.vertexCount)return Status::InvalidData;
    ++context.triangles;
    context.triangleSide=batch.material.side;
    if(batch.closedTriangleComponents){
      const uint16_t component=batch.closedTriangleComponents[i/3];
      if(component!=UINT16_MAX)context.triangleSide=batch.closedSides[component];
    }
    // With cached screen coordinates, facing needs no varying projection or
    // shader selection. Keep clipped geometry on the original clipping path.
    if(batch.screenProjected&&context.triangleSide!=Side::Double){
      const auto& va=batch.vertices[a];const auto& vb=batch.vertices[b];const auto& vc=batch.vertices[c];
      const float area=(vb.x-va.x)*(vc.y-va.y)-(vb.y-va.y)*(vc.x-va.x);
      if(!std::isfinite(area)||std::abs(area)<1e-7f)continue;
      const bool front=(area<0)!=batch.reverseWinding;
      if((context.triangleSide==Side::Front&&!front)||(context.triangleSide==Side::Back&&front))continue;
    }
    drawClipped(batch.vertices[a],batch.vertices[b],batch.vertices[c],
      batch.material,batch.reverseWinding,context);
  }
  return Status::Ok;
}

void Renderer::drawClipped(const ClipVertex &a,const ClipVertex &b,const ClipVertex &c,
                           const Material &material,bool reverseWinding,RasterContext& context) {
  auto distance=[](const ClipVertex &v,unsigned plane) {
    switch(plane) { case 0:return v.w+v.x;case 1:return v.w-v.x;case 2:return v.w+v.y;
      case 3:return v.w-v.y;case 4:return v.w+v.z;default:return v.w-v.z; }
  };
  const unsigned maskA=a.clipMask,maskB=b.clipMask,maskC=c.clipMask;
  if(maskA&maskB&maskC)return;
  if(!(maskA|maskB|maskC)){rasterize(a,b,c,material,reverseWinding,context);return;}
  ++context.clippedTriangles;
  ClipVertex storage[2][12];storage[0][0]=a;storage[0][1]=b;storage[0][2]=c;
  unsigned input=0,count=3;
  auto lerp=[](const ClipVertex &x,const ClipVertex &y,float t) {
    ClipVertex out;
    out.x=x.x+(y.x-x.x)*t;out.y=x.y+(y.y-x.y)*t;out.z=x.z+(y.z-x.z)*t;out.w=x.w+(y.w-x.w)*t;
    out.u=x.u+(y.u-x.u)*t;out.v=x.v+(y.v-x.v)*t;
    out.r=x.r+(y.r-x.r)*t;out.g=x.g+(y.g-x.g)*t;out.b=x.b+(y.b-x.b)*t;
    out.inverseW=out.w>1e-9f?1.0f/out.w:0.0f;out.clipMask=0;return out;
  };
  for(unsigned plane=0;plane<6&&count>=3;++plane) {
    unsigned output=1-input,written=0;
    ClipVertex previous=storage[input][count-1];float pd=distance(previous,plane);
    for(unsigned i=0;i<count;++i) {
      const ClipVertex current=storage[input][i];const float cd=distance(current,plane);
      if((pd>=0)!=(cd>=0)) {
        if(written>=12)return;
        storage[output][written++]=lerp(previous,current,pd/(pd-cd));
      }
      if(cd>=0){if(written>=12)return;storage[output][written++]=current;}
      previous=current;pd=cd;
    }
    input=output;count=written;
  }
  for(unsigned i=1;i+1<count;++i)rasterize(storage[input][0],storage[input][i],storage[input][i+1],material,reverseWinding,context);
}

void Renderer::rasterize(const ClipVertex &ca,const ClipVertex &cb,const ClipVertex &cc,
                         const Material &material,bool reverseWinding,RasterContext& context) {
  const bool flat=std::abs(ca.r-cb.r)+std::abs(ca.r-cc.r)+std::abs(ca.g-cb.g)+
      std::abs(ca.g-cc.g)+std::abs(ca.b-cb.b)+std::abs(ca.b-cc.b)<1e-6f;
  if(material.map){
    if(material.map->tiles_)rasterizeTextured<true>(ca,cb,cc,material,reverseWinding,context,flat);
    else rasterizeTextured<false>(ca,cb,cc,material,reverseWinding,context,flat);
  }else if(flat){
    if(transparent(material))rasterizeVariant<false,true,false>(ca,cb,cc,material,reverseWinding,context);
    else rasterizeVariant<false,true,true>(ca,cb,cc,material,reverseWinding,context);
  }else rasterizeVariant<false,false,false>(ca,cb,cc,material,reverseWinding,context);
}

template<bool Tiled>
void Renderer::rasterizeTextured(const ClipVertex &ca,const ClipVertex &cb,const ClipVertex &cc,
                         const Material &material,bool reverseWinding,RasterContext& context,bool flat){
  const bool opaqueCoverage=!material.map->alpha&&
    unsigned(clamp01(material.opacity)*255+.5f)==255;
  if(flat){
    const bool opaqueTexel=!material.map->alpha&&
      unsigned(clamp01(material.opacity)*255+.5f)==255&&
      unsigned(clamp01(ca.r)*255+.5f)==255&&
      unsigned(clamp01(ca.g)*255+.5f)==255&&
      unsigned(clamp01(ca.b)*255+.5f)==255;
    if(opaqueTexel){
      if constexpr(Tiled)rasterizeOpaqueTiledIram(ca,cb,cc,material,reverseWinding,context);
      else rasterizeVariant<true,true,false,true,false,Tiled>(ca,cb,cc,material,reverseWinding,context);
    }
    else if(opaqueCoverage)rasterizeVariant<true,true,false,false,true,Tiled>(ca,cb,cc,material,reverseWinding,context);
    else rasterizeVariant<true,true,false,false,false,Tiled>(ca,cb,cc,material,reverseWinding,context);
  }
  else if(opaqueCoverage)rasterizeVariant<true,false,false,false,true,Tiled>(ca,cb,cc,material,reverseWinding,context);
  else rasterizeVariant<true,false,false,false,false,Tiled>(ca,cb,cc,material,reverseWinding,context);
}

template<bool Textured,bool Flat,bool Solid,bool OpaqueTexel,bool OpaqueCoverage,bool Tiled>
void Renderer::rasterizeVariant(const ClipVertex &ca,const ClipVertex &cb,const ClipVertex &cc,
                                const Material &material,bool reverseWinding,RasterContext& context) {
  // Compile-time channel packing removes unused attributes from projection,
  // gradient setup, scanline interpolation and every perspective span.
  constexpr unsigned AttributeCount=Solid?1:2+(Textured?2:0)+(Flat?0:3);
  constexpr unsigned VaryingCount=AttributeCount>2?AttributeCount-2:0;
  constexpr unsigned ColorOffset=Textured?2:0;
  struct Projected {float x,y;float q[AttributeCount];};
  auto project=[&](const ClipVertex &v) {
    const float iw=v.inverseW;
    Projected result;
    if(context.batch->screenProjected){
      result.x=v.x;result.y=v.y;result.q[0]=v.z;
      if constexpr(Textured){result.q[2]=v.u;result.q[3]=v.v;}
    }else{
      result.x=(v.x*iw+1)*context.surface.width*0.5f;result.y=(1-v.y*iw)*context.surface.height*0.5f;
      result.q[0]=v.z*iw;
      if constexpr(Textured){result.q[2]=v.u*iw;result.q[3]=v.v*iw;}
    }
    if constexpr(!Solid)result.q[1]=iw;
    if constexpr(!Flat){result.q[2+ColorOffset]=v.r*iw;result.q[3+ColorOffset]=v.g*iw;result.q[4+ColorOffset]=v.b*iw;}
    return result;
  };
  if(ca.w<=1e-9f||cb.w<=1e-9f||cc.w<=1e-9f)return;
  Projected a=project(ca),b=project(cb),c=project(cc);
  const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
  if(!std::isfinite(area)||std::abs(area)<1e-7f)return;
  const bool front=(area<0)!=reverseWinding;
  if((context.triangleSide==Side::Front&&!front)||(context.triangleSide==Side::Back&&front))return;
  const unsigned opacity=unsigned(clamp01(material.opacity)*255+0.5f);
  if(float(opacity)/255.0f<material.alphaTest)return;
  ++context.drawnTriangles;
  // A second raster context repeats projection, but triangles entirely outside
  // its rows need no attribute gradients or edge setup. Use the same half-open
  // scanline bounds as half(); surviving edges retain their original rounding.
  const float minimumY=std::min(a.y,std::min(b.y,c.y));
  const float maximumY=std::max(a.y,std::max(b.y,c.y));
  const int first=std::max(0,rasterCeilToInt(minimumY-0.5f));
  const int last=std::min(int(context.surface.height),rasterCeilToInt(maximumY-0.5f));
  if(first>=int(context.lastRow)||last<=int(context.firstRow))return;
  const float inverseArea=1.0f/area;
  float dx[AttributeCount],dy[AttributeCount];
  for(unsigned k=0;k<AttributeCount;++k) {
    dx[k]=((b.q[k]-a.q[k])*(c.y-a.y)-(c.q[k]-a.q[k])*(b.y-a.y))*inverseArea;
    dy[k]=((b.x-a.x)*(c.q[k]-a.q[k])-(c.x-a.x)*(b.q[k]-a.q[k]))*inverseArea;
  }
  const Projected origin=a;
  if(a.y>b.y)std::swap(a,b);if(b.y>c.y)std::swap(b,c);if(a.y>b.y)std::swap(a,b);
  const Texture *texture=Textured?material.map:nullptr;
  const uint16_t* texturePixels=nullptr;uint32_t tileColumns=0;const uint32_t* compactBlocks=nullptr;
  if constexpr(Textured){
    if constexpr(Tiled){texturePixels=texture->tiles_->pixels;tileColumns=texture->tiles_->columns;compactBlocks=texture->tiles_->compactBlocks;}
    else texturePixels=texture->pixels;
  }
  const uint16_t solidColor=pack(ca.r,ca.g,ca.b);
  const unsigned tintR=unsigned(clamp01(ca.r)*255+.5f),tintG=unsigned(clamp01(ca.g)*255+.5f),tintB=unsigned(clamp01(ca.b)*255+.5f);
  const bool untinted=Flat&&tintR==255&&tintG==255&&tintB==255;
  const auto tintRows=detail::rgb565_tint::selectRows(uint8_t(tintR),uint8_t(tintG),uint8_t(tintB));
  const bool alphaBlended=transparent(material);
  const bool testDepth=context.depth&&material.depthTest,writeDepth=context.depth&&material.depthWrite;
  auto half=[=,&context](const Projected &shortA,const Projected &shortB) {
    uint32_t shadedPixels=0;
    if(shortB.y-shortA.y<1e-7f||c.y-a.y<1e-7f)return;
    const int first=std::max(0,rasterCeilToInt(shortA.y-0.5f));
    const int last=std::min(int(context.surface.height),rasterCeilToInt(shortB.y-0.5f));
    const float longSlope=(c.x-a.x)/(c.y-a.y),shortSlope=(shortB.x-shortA.x)/(shortB.y-shortA.y);
    float lx=a.x+(float(first)+0.5f-a.y)*longSlope;
    float sx=shortA.x+(float(first)+0.5f-shortA.y)*shortSlope;
    for(int y=first;y<last;++y,lx+=longSlope,sx+=shortSlope) {
      // Preserve the original edge accumulation before this context's rows.
      // Starting lx/sx directly at firstRow changes rounding at shared edges.
      if(y<context.firstRow)continue;
      if(y>=context.lastRow)break;
      const int left=std::max(0,rasterCeilToInt(std::min(lx,sx)-0.5f));
      const int right=std::min(int(context.surface.width),rasterCeilToInt(std::max(lx,sx)-0.5f));
      if(left>=right)continue;
      if constexpr(Textured&&Flat&&(OpaqueTexel||OpaqueCoverage)){
        if(right-left==1){
          uint16_t *oneDst=context.surface.pixels+size_t(y)*context.surface.stride+left;
          uint16_t *oneDepth=context.depth?context.depth+size_t(y)*context.surface.width+left:nullptr;
          const float px=float(left)+0.5f-origin.x,py=float(y)+0.5f-origin.y;
          const float z=origin.q[0]+dx[0]*px+dy[0]*py;
          const uint16_t zi=uint16_t(std::fmax(0.0f,std::fmin(65534.0f,(z+1)*32767.0f)));
          if(!testDepth||zi<*oneDepth){
            const float w=origin.q[1]+dx[1]*px+dy[1]*py;
            // Preserve the established one-pixel span endpoint rejection.
            if(w>0&&w+dx[1]>0){
              const float inverseW=1.0f/w;
              const float u=(origin.q[2]+dx[2]*px+dy[2]*py)*inverseW;
              const float v=(origin.q[3]+dx[3]*px+dy[3]*py)*inverseW;
              const uint32_t tx=texelCoordinate(u,texture->width,texture->wrapS);
              float tv=Texture::wrap(v,texture->wrapT);
              if(texture->flipY)tv=1-tv;
              const uint32_t ty=std::min(texture->height-1,uint32_t(tv*float(texture->height)));
              const uint16_t texel=sampleTextureTile<Tiled>(tx,ty,*texture,texturePixels,tileColumns,compactBlocks);
              if constexpr(OpaqueTexel)*oneDst=texel;
              else *oneDst=untinted?texel:detail::rgb565_tint::sample(texel,tintRows);
              if(writeDepth)*oneDepth=zi;
              ++shadedPixels;
            }
          }
          continue;
        }
      }
      float q[AttributeCount];
      for(unsigned k=0;k<AttributeCount;++k)q[k]=origin.q[k]+dx[k]*(float(left)+0.5f-origin.x)+dy[k]*(float(y)+0.5f-origin.y);
      uint16_t *dst=context.surface.pixels+size_t(y)*context.surface.stride+left;
      uint16_t *zbuffer=context.depth?context.depth+size_t(y)*context.surface.width+left:nullptr;
      if constexpr(Solid) {
        float z=q[0];
        for(int x=left;x<right;++x,++dst,z+=dx[0]) {
          const uint16_t zi=uint16_t(std::fmax(0.0f,std::fmin(65534.0f,(z+1)*32767.0f)));
          if(!testDepth||zi<*zbuffer) {*dst=solidColor;if(writeDepth)*zbuffer=zi;++shadedPixels;}
          if(zbuffer)++zbuffer;
        }
      } else {
      float nextInverseW=q[1]>0?1.0f/q[1]:0;
      for(int x=left;x<right;) {
        const int block=std::min(int(context.perspectiveSpan),right-x);
        if(q[1]<=0)break;
        const float startInv=nextInverseW,endW=q[1]+dx[1]*block;
        if(endW<=0)break;
        const float endInv=1.0f/endW;
        nextInverseW=endInv;
        float values[VaryingCount?VaryingCount:1],steps[VaryingCount?VaryingCount:1];
        const float inverseBlock=spanReciprocals[block];
        // Power-of-two division is exactly a binary exponent adjustment.
        // Preserve original division on odd tail spans to retain bit parity.
        const bool exactReciprocal=(block&(block-1))==0;
        for(unsigned k=0;k<VaryingCount;++k) {
          values[k]=q[k+2]*startInv;
          const float difference=(q[k+2]+dx[k+2]*block)*endInv-values[k];
          steps[k]=exactReciprocal?difference*inverseBlock:difference/block;
        }
        float z=q[0];
        for(int p=0;p<block;++p,++dst,z+=dx[0]) {
          const uint16_t zi=uint16_t(std::fmax(0.0f,std::fmin(65534.0f,(z+1)*32767.0f)));
          if(!testDepth||zi<*zbuffer) {
            if constexpr(OpaqueTexel){
              const uint32_t tx=texelCoordinate(values[0],texture->width,texture->wrapS);
              float tv=Texture::wrap(values[1],texture->wrapT);
              if(texture->flipY)tv=1-tv;
              // wrap() returns a finite value in [0,1], including invalid input.
              const uint32_t ty=std::min(texture->height-1,uint32_t(tv*float(texture->height)));
              *dst=sampleTextureTile<Tiled>(tx,ty,*texture,texturePixels,tileColumns,compactBlocks);
              if(writeDepth)*zbuffer=zi;
              ++shadedPixels;
            }else{
            float r=ca.r,g=ca.g,bl=ca.b;
            if constexpr(!Flat){r=values[ColorOffset];g=values[ColorOffset+1];bl=values[ColorOffset+2];}
            unsigned alpha=opacity;
            uint16_t color=solidColor;
            if constexpr(Textured) {
              const uint32_t tx=texelCoordinate(values[0],texture->width,texture->wrapS);
              float tv=Texture::wrap(values[1],texture->wrapT);
              if(texture->flipY)tv=1-tv;
              const uint32_t ty=std::min(texture->height-1,uint32_t(tv*float(texture->height)));
              const uint16_t texel=sampleTextureTile<Tiled>(tx,ty,*texture,texturePixels,tileColumns,compactBlocks);
              if constexpr(Flat)color=untinted?texel:detail::rgb565_tint::sample(texel,tintRows);
              else {float tr,tg,tb;unpack(texel,tr,tg,tb);r*=tr;g*=tg;bl*=tb;color=pack(r,g,bl);}
              if constexpr(!OpaqueCoverage){if(texture->alpha)alpha=(alpha*texture->alpha[size_t(ty)*texture->stride+tx]+127)/255;}
            } else if constexpr(!Flat)color=pack(r,g,bl);
            if constexpr(OpaqueCoverage){
              *dst=color;if(writeDepth)*zbuffer=zi;++shadedPixels;
            }else if(float(alpha)/255.0f>=material.alphaTest && (alpha||!alphaBlended)) {
              // Alpha-test materials retain opaque coverage after the cutoff.
              if(!alphaBlended)alpha=255;
              *dst=blend565(color,*dst,alpha);if(writeDepth)*zbuffer=zi;++shadedPixels;
            }
            }
          }
          if(zbuffer)++zbuffer;
          for(unsigned k=0;k<VaryingCount;++k)values[k]+=steps[k];
        }
        for(unsigned k=0;k<AttributeCount;++k)q[k]+=dx[k]*block;
        x+=block;
      }
      }
    }
    context.shadedPixels+=shadedPixels;
  };
  half(a,b);half(b,c);
}
NICE_GFX_3D_HOT_SHADER_SECTION void Renderer::rasterizeOpaqueTiledIram(const ClipVertex &ca,const ClipVertex &cb,const ClipVertex &cc,
                                const Material &material,bool reverseWinding,RasterContext& context) {
  constexpr bool Textured=true,Flat=true,Solid=false,OpaqueTexel=true,OpaqueCoverage=false,Tiled=true;
  // Compile-time channel packing removes unused attributes from projection,
  // gradient setup, scanline interpolation and every perspective span.
  constexpr unsigned AttributeCount=Solid?1:2+(Textured?2:0)+(Flat?0:3);
  constexpr unsigned VaryingCount=AttributeCount>2?AttributeCount-2:0;
  constexpr unsigned ColorOffset=Textured?2:0;
  struct Projected {float x,y;float q[AttributeCount];};
  auto project=[&](const ClipVertex &v) {
    const float iw=v.inverseW;
    Projected result;
    if(context.batch->screenProjected){
      result.x=v.x;result.y=v.y;result.q[0]=v.z;
      if constexpr(Textured){result.q[2]=v.u;result.q[3]=v.v;}
    }else{
      result.x=(v.x*iw+1)*context.surface.width*0.5f;result.y=(1-v.y*iw)*context.surface.height*0.5f;
      result.q[0]=v.z*iw;
      if constexpr(Textured){result.q[2]=v.u*iw;result.q[3]=v.v*iw;}
    }
    if constexpr(!Solid)result.q[1]=iw;
    if constexpr(!Flat){result.q[2+ColorOffset]=v.r*iw;result.q[3+ColorOffset]=v.g*iw;result.q[4+ColorOffset]=v.b*iw;}
    return result;
  };
  if(ca.w<=1e-9f||cb.w<=1e-9f||cc.w<=1e-9f)return;
  Projected a=project(ca),b=project(cb),c=project(cc);
  const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
  if(!std::isfinite(area)||std::abs(area)<1e-7f)return;
  const bool front=(area<0)!=reverseWinding;
  if((context.triangleSide==Side::Front&&!front)||(context.triangleSide==Side::Back&&front))return;
  const unsigned opacity=unsigned(clamp01(material.opacity)*255+0.5f);
  if(float(opacity)/255.0f<material.alphaTest)return;
  ++context.drawnTriangles;
  // A second raster context repeats projection, but triangles entirely outside
  // its rows need no attribute gradients or edge setup. Use the same half-open
  // scanline bounds as half(); surviving edges retain their original rounding.
  const float minimumY=std::min(a.y,std::min(b.y,c.y));
  const float maximumY=std::max(a.y,std::max(b.y,c.y));
  const int first=std::max(0,rasterCeilToInt(minimumY-0.5f));
  const int last=std::min(int(context.surface.height),rasterCeilToInt(maximumY-0.5f));
  if(first>=int(context.lastRow)||last<=int(context.firstRow))return;
  const float inverseArea=1.0f/area;
  float dx[AttributeCount],dy[AttributeCount];
  for(unsigned k=0;k<AttributeCount;++k) {
    dx[k]=((b.q[k]-a.q[k])*(c.y-a.y)-(c.q[k]-a.q[k])*(b.y-a.y))*inverseArea;
    dy[k]=((b.x-a.x)*(c.q[k]-a.q[k])-(c.x-a.x)*(b.q[k]-a.q[k]))*inverseArea;
  }
  const Projected origin=a;
  if(a.y>b.y)std::swap(a,b);if(b.y>c.y)std::swap(b,c);if(a.y>b.y)std::swap(a,b);
  const Texture *texture=Textured?material.map:nullptr;
  const uint16_t* texturePixels=nullptr;uint32_t tileColumns=0;const uint32_t* compactBlocks=nullptr;
  if constexpr(Textured){
    if constexpr(Tiled){texturePixels=texture->tiles_->pixels;tileColumns=texture->tiles_->columns;compactBlocks=texture->tiles_->compactBlocks;}
    else texturePixels=texture->pixels;
  }
  const uint16_t solidColor=pack(ca.r,ca.g,ca.b);
  const unsigned tintR=unsigned(clamp01(ca.r)*255+.5f),tintG=unsigned(clamp01(ca.g)*255+.5f),tintB=unsigned(clamp01(ca.b)*255+.5f);
  const bool untinted=Flat&&tintR==255&&tintG==255&&tintB==255;
  const auto tintRows=detail::rgb565_tint::selectRows(uint8_t(tintR),uint8_t(tintG),uint8_t(tintB));
  const bool alphaBlended=transparent(material);
  const bool testDepth=context.depth&&material.depthTest,writeDepth=context.depth&&material.depthWrite;
  auto half=[=,&context](const Projected &shortA,const Projected &shortB) NICE_GFX_3D_HOT_SHADER_SECTION {
    uint32_t shadedPixels=0;
    if(shortB.y-shortA.y<1e-7f||c.y-a.y<1e-7f)return;
    const int first=std::max(0,rasterCeilToInt(shortA.y-0.5f));
    const int last=std::min(int(context.surface.height),rasterCeilToInt(shortB.y-0.5f));
    const float longSlope=(c.x-a.x)/(c.y-a.y),shortSlope=(shortB.x-shortA.x)/(shortB.y-shortA.y);
    float lx=a.x+(float(first)+0.5f-a.y)*longSlope;
    float sx=shortA.x+(float(first)+0.5f-shortA.y)*shortSlope;
    for(int y=first;y<last;++y,lx+=longSlope,sx+=shortSlope) {
      // Preserve the original edge accumulation before this context's rows.
      // Starting lx/sx directly at firstRow changes rounding at shared edges.
      if(y<context.firstRow)continue;
      if(y>=context.lastRow)break;
      const int left=std::max(0,rasterCeilToInt(std::min(lx,sx)-0.5f));
      const int right=std::min(int(context.surface.width),rasterCeilToInt(std::max(lx,sx)-0.5f));
      if(left>=right)continue;
      if constexpr(Textured&&Flat&&(OpaqueTexel||OpaqueCoverage)){
        if(right-left==1){
          uint16_t *oneDst=context.surface.pixels+size_t(y)*context.surface.stride+left;
          uint16_t *oneDepth=context.depth?context.depth+size_t(y)*context.surface.width+left:nullptr;
          const float px=float(left)+0.5f-origin.x,py=float(y)+0.5f-origin.y;
          const float z=origin.q[0]+dx[0]*px+dy[0]*py;
          const uint16_t zi=uint16_t(std::fmax(0.0f,std::fmin(65534.0f,(z+1)*32767.0f)));
          if(!testDepth||zi<*oneDepth){
            const float w=origin.q[1]+dx[1]*px+dy[1]*py;
            // Preserve the established one-pixel span endpoint rejection.
            if(w>0&&w+dx[1]>0){
              const float inverseW=1.0f/w;
              const float u=(origin.q[2]+dx[2]*px+dy[2]*py)*inverseW;
              const float v=(origin.q[3]+dx[3]*px+dy[3]*py)*inverseW;
              const uint32_t tx=texelCoordinate(u,texture->width,texture->wrapS);
              float tv=Texture::wrap(v,texture->wrapT);
              if(texture->flipY)tv=1-tv;
              const uint32_t ty=std::min(texture->height-1,uint32_t(tv*float(texture->height)));
              const uint16_t texel=sampleTextureTile<Tiled>(tx,ty,*texture,texturePixels,tileColumns,compactBlocks);
              if constexpr(OpaqueTexel)*oneDst=texel;
              else *oneDst=untinted?texel:detail::rgb565_tint::sample(texel,tintRows);
              if(writeDepth)*oneDepth=zi;
              ++shadedPixels;
            }
          }
          continue;
        }
      }
      float q[AttributeCount];
      for(unsigned k=0;k<AttributeCount;++k)q[k]=origin.q[k]+dx[k]*(float(left)+0.5f-origin.x)+dy[k]*(float(y)+0.5f-origin.y);
      uint16_t *dst=context.surface.pixels+size_t(y)*context.surface.stride+left;
      uint16_t *zbuffer=context.depth?context.depth+size_t(y)*context.surface.width+left:nullptr;
      if constexpr(Solid) {
        float z=q[0];
        for(int x=left;x<right;++x,++dst,z+=dx[0]) {
          const uint16_t zi=uint16_t(std::fmax(0.0f,std::fmin(65534.0f,(z+1)*32767.0f)));
          if(!testDepth||zi<*zbuffer) {*dst=solidColor;if(writeDepth)*zbuffer=zi;++shadedPixels;}
          if(zbuffer)++zbuffer;
        }
      } else {
      float nextInverseW=q[1]>0?1.0f/q[1]:0;
      for(int x=left;x<right;) {
        const int block=std::min(int(context.perspectiveSpan),right-x);
        if(q[1]<=0)break;
        const float startInv=nextInverseW,endW=q[1]+dx[1]*block;
        if(endW<=0)break;
        const float endInv=1.0f/endW;
        nextInverseW=endInv;
        float values[VaryingCount?VaryingCount:1],steps[VaryingCount?VaryingCount:1];
        const float inverseBlock=spanReciprocals[block];
        // Power-of-two division is exactly a binary exponent adjustment.
        // Preserve original division on odd tail spans to retain bit parity.
        const bool exactReciprocal=(block&(block-1))==0;
        for(unsigned k=0;k<VaryingCount;++k) {
          values[k]=q[k+2]*startInv;
          const float difference=(q[k+2]+dx[k+2]*block)*endInv-values[k];
          steps[k]=exactReciprocal?difference*inverseBlock:difference/block;
        }
        float z=q[0];
        for(int p=0;p<block;++p,++dst,z+=dx[0]) {
          const uint16_t zi=uint16_t(std::fmax(0.0f,std::fmin(65534.0f,(z+1)*32767.0f)));
          if(!testDepth||zi<*zbuffer) {
            if constexpr(OpaqueTexel){
              const uint32_t tx=texelCoordinate(values[0],texture->width,texture->wrapS);
              float tv=Texture::wrap(values[1],texture->wrapT);
              if(texture->flipY)tv=1-tv;
              // wrap() returns a finite value in [0,1], including invalid input.
              const uint32_t ty=std::min(texture->height-1,uint32_t(tv*float(texture->height)));
              *dst=sampleTextureTile<Tiled>(tx,ty,*texture,texturePixels,tileColumns,compactBlocks);
              if(writeDepth)*zbuffer=zi;
              ++shadedPixels;
            }else{
            float r=ca.r,g=ca.g,bl=ca.b;
            if constexpr(!Flat){r=values[ColorOffset];g=values[ColorOffset+1];bl=values[ColorOffset+2];}
            unsigned alpha=opacity;
            uint16_t color=solidColor;
            if constexpr(Textured) {
              const uint32_t tx=texelCoordinate(values[0],texture->width,texture->wrapS);
              float tv=Texture::wrap(values[1],texture->wrapT);
              if(texture->flipY)tv=1-tv;
              const uint32_t ty=std::min(texture->height-1,uint32_t(tv*float(texture->height)));
              const uint16_t texel=sampleTextureTile<Tiled>(tx,ty,*texture,texturePixels,tileColumns,compactBlocks);
              if constexpr(Flat)color=untinted?texel:detail::rgb565_tint::sample(texel,tintRows);
              else {float tr,tg,tb;unpack(texel,tr,tg,tb);r*=tr;g*=tg;bl*=tb;color=pack(r,g,bl);}
              if constexpr(!OpaqueCoverage){if(texture->alpha)alpha=(alpha*texture->alpha[size_t(ty)*texture->stride+tx]+127)/255;}
            } else if constexpr(!Flat)color=pack(r,g,bl);
            if constexpr(OpaqueCoverage){
              *dst=color;if(writeDepth)*zbuffer=zi;++shadedPixels;
            }else if(float(alpha)/255.0f>=material.alphaTest && (alpha||!alphaBlended)) {
              // Alpha-test materials retain opaque coverage after the cutoff.
              if(!alphaBlended)alpha=255;
              *dst=blend565(color,*dst,alpha);if(writeDepth)*zbuffer=zi;++shadedPixels;
            }
            }
          }
          if(zbuffer)++zbuffer;
          for(unsigned k=0;k<VaryingCount;++k)values[k]+=steps[k];
        }
        for(unsigned k=0;k<AttributeCount;++k)q[k]+=dx[k]*block;
        x+=block;
      }
      }
    }
    context.shadedPixels+=shadedPixels;
  };
  half(a,b);half(b,c);
}

}
