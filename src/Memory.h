#pragma once
#include "Config.h"
#include <cstddef>
#include <cstdint>

namespace nice_gfx_3d {
enum class MemoryPreference { InternalFirst, ExternalFirst, InternalOnly };
namespace detail {
void *allocate(size_t bytes, MemoryPreference preference, size_t alignment = 128);
void release(void *memory);
uint64_t microseconds();
}
}
