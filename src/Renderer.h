#pragma once
#define NICE_GFX_3D_HAS_BASIC_CONSTANT_VERTEX_PATH 1
#include "Config.h"
#include "Status.h"
#include "Memory.h"
#include "Scene.h"
#include <cstddef>
#include <cstdint>
#define NICE_GFX_3D_HAS_PHASE_STATS 1
#define NICE_GFX_3D_HAS_CLEAR_STATS 1
#define NICE_GFX_3D_HAS_ASYNC_CLEAR 1
#define NICE_GFX_3D_HAS_RENDER_TO_SURFACE 1
#define NICE_GFX_3D_HAS_RASTER_WORKERS 1
#define NICE_GFX_3D_HAS_RASTER_TIMING 1
#define NICE_GFX_3D_HAS_TEXTURE_TILES 1
#define NICE_GFX_3D_HAS_VERTEX_WORKERS 1
#define NICE_GFX_3D_HAS_HOT_SHADER_IRAM 1
#define NICE_GFX_3D_HAS_BALANCED_RASTER 1
#define NICE_GFX_3D_HAS_CLOSED_SURFACES 1
#define NICE_GFX_3D_HAS_PROJECTED_CACHE 1

namespace nice_gfx_3d {

// Native, little-endian RGB565 words. Borrowed until resize/end/destruction.
struct RenderSurface {
  uint16_t *pixels = nullptr;
  uint16_t width = 0, height = 0;
  size_t stride = 0;
  bool valid() const { return pixels && width && height && stride >= width; }
};

struct RendererOptions {
  uint16_t width = 320, height = 240;
  uint32_t maxVertices = 4096, maxObjects = 128;
  uint16_t clearColor = 0;
  bool depthTest = true;
  // Optional P4 PPA overlap. Renderer still returns only after all clear DMA
  // and raster work finish. Unsupported ports keep ordinary blocking/CPU clear.
  bool asyncClear=false;
  // Fast perspective correction uses one reciprocal per span of N pixels.
  // 1 gives exact perspective per pixel; 8 is a useful embedded default.
  uint8_t perspectiveSpan = 8;
  // Two workers share transformed vertices and rasterize disjoint row ranges.
  // Device setup reserves one 16 KiB helper stack. Failure retains one worker.
  uint8_t rasterWorkers = 1;
  bool parallelVertexWorkers=true;
  // Optional device timing feedback redistributes rows between both workers.
  // Adds two coarse timers per mesh; never changes triangle order or shading.
  bool balanceRasterWorkers=false;
  MemoryPreference colorMemory = MemoryPreference::InternalFirst;
  MemoryPreference depthMemory = MemoryPreference::ExternalFirst;
  MemoryPreference scratchMemory = MemoryPreference::ExternalFirst;
  // Optional coarse profiling, never inside the vertex or pixel loop.
  bool measurePhases = false;
};

struct RenderStats {
  uint32_t objects = 0, meshes = 0, culledMeshes = 0;
  uint32_t vertices = 0, triangles = 0, clippedTriangles = 0, drawnTriangles = 0;
  uint32_t shadedPixels = 0;
  uint32_t reusedPositions = 0, reusedLighting = 0;
  uint32_t vertexWorkerJobs=0,vertexWorkerFallbacks=0;
  uint64_t ownerVertexMicros=0,helperVertexMicros=0,vertexJoinWaitMicros=0;
  uint32_t tiledTextureMeshes = 0;
  uint32_t compactTextureMeshes=0;
  uint32_t closedSurfaceComponents=0,closedSurfaceFallbacks=0;
  uint32_t cachedProjectionMeshes = 0, cachedProjectionVertices = 0;
  uint32_t clearPpaFills = 0, clearCpuFills = 0, clearPpaFallbacks = 0;
  uint32_t asyncClearFills=0,asyncClearFallbacks=0;
  uint64_t clearSubmitMicros=0,clearWaitMicros=0;
  uint32_t rasterWorkerJobs = 0, rasterWorkerFallbacks = 0;
  uint32_t rasterWorkerStackFreeBytes = 0;
  uint8_t rasterWorkersUsed = 1;
  uint16_t rasterSplitRow=0;
  int8_t rasterOwnerCore = -1, rasterHelperCore = -1;
  uint64_t renderMicros = 0;
  uint64_t worldMicros = 0, clearMicros = 0, skinMicros = 0;
  uint64_t vertexMicros = 0, triangleMicros = 0;
  uint64_t ownerRasterMicros = 0, helperRasterMicros = 0, rasterJoinWaitMicros = 0;
  size_t bufferBytes = 0, scratchBytes = 0;
  uint32_t basicConstantMeshes=0;
};

// Initialization allocates bounded scratch space. Rendering does not allocate,
// acquire a display, or invoke virtual methods inside the raster pixel loop.
// One renderer has one owner task. Use a separate presenter to publish frames.
class Renderer {
 public:
  Renderer() = default;
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Status begin(const RendererOptions &options = RendererOptions());
  // Explicit resize may allocate. Call outside animation/render callbacks.
  Status setSize(uint16_t width,uint16_t height) {
    RendererOptions next=options_;next.width=width;next.height=height;return begin(next);
  }
  void end();
  Status render(Object3D &scene, Camera &camera);
  // Synchronously borrow a packed, same-sized RGB565 colour target. The caller
  // owns its allocation and retains it through this call. Depth and scratch
  // remain renderer-owned. frame() resumes exposing the owned colour buffer
  // when this call returns, including on failure. One owner task only.
  Status renderToSurface(Object3D &scene, Camera &camera, const RenderSurface &target);
  const RenderSurface &frame() const { return surface_; }
  const RenderStats &stats() const { return stats_; }
  const RendererOptions &options() const { return options_; }
  void setClearColor(uint16_t color) { options_.clearColor = color; }
  bool ready() const { return surface_.valid(); }
  // One owner, between render calls. No allocation or task is added here.
  void setAsyncClear(bool enabled){options_.asyncClear=enabled;}
  void setVertexWorkers(bool enabled){options_.parallelVertexWorkers=enabled;}
  // Change only between render calls, with the renderer's owner idle.
  void setRasterWorkerBalancing(bool enabled){options_.balanceRasterWorkers=enabled;rasterSplitRow_=surface_.height/2;}

 private:
  struct ClipVertex;
  struct DrawItem;
  struct RasterBatch;
  struct RasterContext;
  struct RasterWorker;
  struct VertexContext;
  struct AsyncClearState;
  struct LightData {
    Vector3 direction;
    float r = 0, g = 0, b = 0;
  };
  RendererOptions options_;
  RenderSurface surface_;
  RenderStats stats_;
  uint16_t *depth_ = nullptr;
  ClipVertex *vertices_ = nullptr;
  DrawItem *draws_ = nullptr;
  Object3D **stack_ = nullptr;
  size_t drawCount_ = 0;
  LightData lights_[4];
  size_t lightCount_ = 0;
  float ambient_[3] = {0, 0, 0};
  Matrix4 viewProjection_;
  void* clearPpa_ = nullptr;
  AsyncClearState* asyncClear_=nullptr;
  bool submitAsyncClear(uint16_t* pixels,uint16_t color);
  void finishPendingClear();
  RasterWorker* rasterWorker_ = nullptr;
  uint16_t rasterSplitRow_=0;
  void prepareRasterWorker();
  void stopRasterTask();
  void releaseRasterWorker();
  bool ensureRasterTask();
  Status waitRasterWorker();
  bool startVertexWorker(VertexContext& context);
  Status joinVertexWorker();
  static Status transformVertices(VertexContext& context);
  static Status transformVerticesBasicConstant(VertexContext& context);
  static Status transformVerticesDispatch(VertexContext& context);
  bool startRasterWorker(const RasterContext& context);
  Status joinRasterWorker(RasterContext& context);
  bool aliasesRasterWorker(uintptr_t first,uintptr_t last) const;
  static void rasterWorkerTask(void* context);
  Status updateWorlds(Object3D &root, bool descendants);
  Status collect(Object3D &root, Camera &camera);
  Status drawMesh(const DrawItem &item, Camera &camera);
  static Status rasterizeBatch(RasterContext& context);
  static void drawClipped(const ClipVertex &a, const ClipVertex &b, const ClipVertex &c,
                   const Material &material, bool reverseWinding,RasterContext& context);
  static void rasterize(const ClipVertex &a, const ClipVertex &b, const ClipVertex &c,
                 const Material &material, bool reverseWinding,RasterContext& context);
  template<bool Tiled>
  static void rasterizeTextured(const ClipVertex &a,const ClipVertex &b,const ClipVertex &c,
                        const Material &material,bool reverseWinding,RasterContext& context,bool flat);
  static void rasterizeOpaqueTiledIram(const ClipVertex &a,const ClipVertex &b,const ClipVertex &c,
                        const Material &material,bool reverseWinding,RasterContext& context);
  template<bool Textured,bool Flat,bool Solid,bool OpaqueTexel=false,bool OpaqueCoverage=false,bool Tiled=false>
  static void rasterizeVariant(const ClipVertex &a,const ClipVertex &b,const ClipVertex &c,
                        const Material &material,bool reverseWinding,RasterContext& context);
};
}
