#pragma once
#include "Status.h"
#include <cstddef>
#include <memory>

#define NICE_GFX_3D_HAS_MODEL_CACHE_WORKSPACE 1
namespace nice_gfx_3d {
namespace detail {struct ModelCacheState;}

// Explicit setup-only owner for reusable immutable-model cache buffers.
// Model::clear() returns leases; this separate owner retains their allocations.
// One Model may lease a workspace at a time. No render may run during changes.
// This prototype reserves at most 32 typed cache blocks and freezes each group
// after its first successful setup. Different layouts return CapacityExceeded.
// This applies to required base maps and texture payloads. Optional raw-attribute
// slots may fall back to ordinary uncached inputs and still return Ok; inspect
// Model's cache byte/mesh diagnostics to detect that ordinary fallback.
class ModelCacheWorkspace {
 public:
  ModelCacheWorkspace()=default;
  ~ModelCacheWorkspace();
  ModelCacheWorkspace(const ModelCacheWorkspace&)=delete;
  ModelCacheWorkspace& operator=(const ModelCacheWorkspace&)=delete;
  Status end();
  size_t reservedBytes()const;
  size_t leasedBytes()const;
  size_t reservedBlocks()const;
  size_t leasedBlocks()const;
 private:
  friend class Model;
  std::shared_ptr<detail::ModelCacheState> state_;
};
}
