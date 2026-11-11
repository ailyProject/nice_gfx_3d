#pragma once
#include <cstdint>

namespace nice_gfx_3d {
enum class Status : uint8_t {
  Ok, InvalidArgument, NotInitialized, NoMemory, CapacityExceeded,
  InvalidData, Unsupported, IoError, Busy
};
inline const char *statusString(Status value) {
  switch (value) {
    case Status::Ok: return "ok";
    case Status::InvalidArgument: return "invalid argument";
    case Status::NotInitialized: return "not initialized";
    case Status::NoMemory: return "out of memory";
    case Status::CapacityExceeded: return "configured capacity exceeded";
    case Status::InvalidData: return "invalid model data";
    case Status::Unsupported: return "unsupported feature or target";
    case Status::IoError: return "I/O error";
    case Status::Busy: return "resource busy";
  }
  return "unknown status";
}
}
