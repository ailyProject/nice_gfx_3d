#include "Memory.h"
#include <cstdint>
#include <cstdlib>
#include <limits>
#if NICE_GFX_3D_ESP
#include "esp_heap_caps.h"
#include "esp_timer.h"
#else
#include <chrono>
#endif

namespace nice_gfx_3d { namespace detail {
void *allocate(size_t bytes, MemoryPreference preference, size_t alignment) {
  if (!bytes || alignment < alignof(void *) || (alignment & (alignment - 1))) return nullptr;
#if NICE_GFX_3D_ESP
  const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  void *result = heap_caps_aligned_alloc(alignment, bytes,
      preference == MemoryPreference::ExternalFirst ? external : internal);
  if (!result && preference != MemoryPreference::InternalOnly)
    result = heap_caps_aligned_alloc(alignment, bytes,
        preference == MemoryPreference::ExternalFirst ? internal : external);
  return result;
#else
  (void)preference;
  if (bytes > std::numeric_limits<size_t>::max() - alignment - sizeof(void *)) return nullptr;
  void *base = std::malloc(bytes + alignment - 1 + sizeof(void *));
  if (!base) return nullptr;
  const uintptr_t start = reinterpret_cast<uintptr_t>(base) + sizeof(void *);
  const uintptr_t aligned = (start + alignment - 1) & ~(uintptr_t(alignment) - 1);
  reinterpret_cast<void **>(aligned)[-1] = base;
  return reinterpret_cast<void *>(aligned);
#endif
}
void release(void *memory) {
#if NICE_GFX_3D_ESP
  heap_caps_free(memory);
#else
  if (memory) std::free(reinterpret_cast<void **>(memory)[-1]);
#endif
}
uint64_t microseconds() {
#if NICE_GFX_3D_ESP
  return uint64_t(esp_timer_get_time());
#else
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
} }
