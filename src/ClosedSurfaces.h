#pragma once
#include "Math3D.h"
#include <cstdint>
namespace nice_gfx_3d {
struct Vertex;
struct SkinVertex;
// Offline hints describe only rigid, consistently wound, closed components.
// UINT32_MAX joint identifies an unskinned component. No vertices are removed.
struct ClosedSurfaceComponent {
  Vector3 center,halfExtent;
  uint32_t joint=UINT32_MAX;
  int8_t winding=1;
};
struct ClosedSurfaceHints {
  size_t vertexCount=0,triangleCount=0,componentCount=0;
  const ClosedSurfaceComponent* components=nullptr;
  const uint16_t* triangleComponents=nullptr;
};
namespace detail {
struct ClosedSurfaceReuse {
  const ClosedSurfaceHints* hints=nullptr;
  const Vertex* vertices=nullptr;
  const SkinVertex* skinVertices=nullptr;
  const uint32_t* indices=nullptr;
  size_t vertexCount=0,indexCount=0;
};
}
}
