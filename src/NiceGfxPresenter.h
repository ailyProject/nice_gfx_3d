#pragma once
#include "Renderer.h"
#include <Display/Backend.h>
#define NICE_GFX_3D_HAS_PRESENT_PHASE_STATS 1
#define NICE_GFX_3D_HAS_PRESENT_FIT 1

namespace nice_gfx_3d {
struct Viewport {
  int32_t x=0,y=0;
  uint16_t width=0,height=0; // Both zero means the entire display.
};
struct PresentStats {
  uint64_t presentMicros=0;
  // Includes synchronous SDK/cache work within each phase.
  uint64_t prepareMicros=0,beginFrameMicros=0,mapMicros=0;
  uint64_t scaleMicros=0,flushMicros=0,commitMicros=0,abortMicros=0;
  uint64_t barFillMicros=0,barPixels=0;
  uint64_t ppaTargetWritebackMicros=0,ppaSourceWritebackMicros=0;
  uint64_t ppaDriverMicros=0,ppaTargetInvalidateMicros=0;
  uint64_t ppaTargetWritebackBytes=0,ppaSourceWritebackBytes=0,ppaTargetInvalidateBytes=0;
  uint32_t ppaCalls=0,flushCalls=0;
  uint32_t barFillCalls=0;
  bool hardwareScaled=false,mapped=false;
};

// Standalone display owner. Serializes a complete begin/map/blit/commit frame.
// Studio/UI composition uses the media bridge instead: do not call present()
// while a View or another producer has an open frame on the same backend.
class NiceGfxPresenter {
 public:
  NiceGfxPresenter()=default;
  ~NiceGfxPresenter();
  NiceGfxPresenter(const NiceGfxPresenter&)=delete;
  NiceGfxPresenter&operator=(const NiceGfxPresenter&)=delete;
  Status begin(nice_gfx::Backend &display,bool usePpa=true);
  void end();
  Status present(const RenderSurface &surface,Viewport viewport=Viewport());
  // Owns and replaces the entire display: fit the source and clear every
  // letterbox pixel. Use present(viewport) for composition that retains peers.
  Status presentFit(const RenderSurface &surface,uint16_t backgroundColor=0);
  // Aspect-preserving destination. Pixels outside this rectangle are retained.
  static Viewport fit(const RenderSurface &surface,uint16_t width,uint16_t height) {
    if(!surface.width||!surface.height||!width||!height)return {};
    uint16_t w=width,h=height;
    if(uint32_t(width)*surface.height>uint32_t(height)*surface.width)
      w=uint16_t(std::max(uint32_t(1),uint32_t(height)*surface.width/surface.height));
    else h=uint16_t(std::max(uint32_t(1),uint32_t(width)*surface.height/surface.width));
    return {int32_t((width-w)/2),int32_t((height-h)/2),w,h};
  }
  const PresentStats&stats()const{return stats_;}
 private:
  nice_gfx::Backend *display_=nullptr;
  void *ppa_=nullptr;
  uint16_t *rows_=nullptr;
  uint32_t *columns_=nullptr;
  size_t rowWidth_=0;
  PresentStats stats_;
  Status presentImpl(const RenderSurface&,Viewport,bool fillBars,uint16_t backgroundColor);
  bool scaleWithPpa(const RenderSurface&,const nice_gfx::NativeFramebuffer&,const Viewport&);
  void scaleRows(const RenderSurface&,uint16_t *out,size_t stride,const Viewport&,size_t first,size_t count);
};
}
