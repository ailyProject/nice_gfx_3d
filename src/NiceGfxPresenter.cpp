#include "NiceGfxPresenter.h"
#include <algorithm>
#include <cstring>
#if NICE_GFX_3D_ESP
#include "driver/ppa.h"
#include "esp_cache.h"
#endif

namespace nice_gfx_3d {
namespace {
#if NICE_GFX_3D_ESP
bool syncCache(void *address,size_t bytes,int flags,uint64_t *actualBytes=nullptr) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
  const size_t line=esp_cache_get_line_size_by_addr(address);
  // S31's internal SRAM has no data cache. Its msync correctly reports
  // NOT_SUPPORTED there, which must not disable the internal-source fast path.
  if(!line)return true;
  if(reinterpret_cast<uintptr_t>(address)%line||bytes%line)return false;
#endif
  if(actualBytes)*actualBytes+=bytes;
  return esp_cache_msync(address,bytes,flags)==ESP_OK;
}
#endif
Status convert(nice_gfx::Status value) {
  switch(value) {
    case nice_gfx::Status::Ok:return Status::Ok;
    case nice_gfx::Status::InvalidArgument:return Status::InvalidArgument;
    case nice_gfx::Status::NotInitialized:return Status::NotInitialized;
    case nice_gfx::Status::NoMemory:return Status::NoMemory;
    case nice_gfx::Status::Unsupported:return Status::Unsupported;
    case nice_gfx::Status::Busy:return Status::Busy;
    default:return Status::IoError;
  }
}
}
NiceGfxPresenter::~NiceGfxPresenter(){end();}
void NiceGfxPresenter::end() {
#if NICE_GFX_3D_ESP
  if(ppa_)ppa_unregister_client(static_cast<ppa_client_handle_t>(ppa_));
#endif
  ppa_=nullptr;display_=nullptr;detail::release(rows_);detail::release(columns_);
  rows_=nullptr;columns_=nullptr;rowWidth_=0;
}
Status NiceGfxPresenter::begin(nice_gfx::Backend &display,bool usePpa) {
  if(!display.ready()||!display.width()||!display.height())return Status::NotInitialized;
  auto *rows=static_cast<uint16_t *>(detail::allocate(size_t(display.width())*16*2,MemoryPreference::ExternalFirst));
  auto *columns=static_cast<uint32_t *>(detail::allocate(size_t(display.width())*4,MemoryPreference::InternalFirst));
  if(!rows||!columns){detail::release(rows);detail::release(columns);return Status::NoMemory;}
  end();display_=&display;rows_=rows;columns_=columns;rowWidth_=display.width();
#if NICE_GFX_3D_ESP
  if(usePpa){ppa_client_config_t config={};config.oper_type=PPA_OPERATION_SRM;config.max_pending_trans_num=1;
    ppa_client_handle_t handle=nullptr;if(ppa_register_client(&config,&handle)==ESP_OK)ppa_=handle;}
#else
  (void)usePpa;
#endif
  return Status::Ok;
}
void NiceGfxPresenter::scaleRows(const RenderSurface &source,uint16_t *out,size_t stride,
                                const Viewport &area,size_t first,size_t count) {
  uint32_t previous=UINT32_MAX;
  for(size_t row=0;row<count;++row) {
    const uint32_t sy=uint32_t(uint64_t(first+row)*source.height/area.height);
    uint16_t *target=out+row*stride;
    if(sy==previous){std::memcpy(target,target-stride,size_t(area.width)*2);continue;}
    const uint16_t *input=source.pixels+size_t(sy)*source.stride;
    if(source.width==area.width)std::memcpy(target,input,size_t(area.width)*2);
    else for(size_t x=0;x<area.width;++x)target[x]=input[columns_[x]];
    previous=sy;
  }
}
bool NiceGfxPresenter::scaleWithPpa(const RenderSurface &source,
    const nice_gfx::NativeFramebuffer &target,const Viewport &area) {
#if NICE_GFX_3D_ESP
  // PPA scales quantize to 1/16. Use it only for exact output dimensions;
  // arbitrary widget sizes retain the exact nearest-neighbour CPU fallback.
  if(!ppa_||source.stride>0x3fff||target.stride>0x3fff||
      (uint32_t(area.width)*16)%source.width||(uint32_t(area.height)*16)%source.height||
      (reinterpret_cast<uintptr_t>(source.pixels)&63)||(reinterpret_cast<uintptr_t>(target.pixels)&63)||
      (source.stride*source.height*2)%64||target.bytes%64)return false;
  const float scaleX=float(area.width)/source.width,scaleY=float(area.height)/source.height;
  if(scaleX<0.0625f||scaleY<0.0625f||scaleX>=256||scaleY>=256)return false;
  // Preserve adjacent pixels in cache lines shared with a widget rectangle.
  uint64_t phase=detail::microseconds();
  const bool targetReady=syncCache(target.pixels,target.bytes,ESP_CACHE_MSYNC_FLAG_DIR_C2M,&stats_.ppaTargetWritebackBytes);
  uint64_t finished=detail::microseconds();stats_.ppaTargetWritebackMicros+=finished-phase;
  if(!targetReady)return false;
  phase=finished;
  const bool sourceReady=syncCache(source.pixels,source.stride*source.height*2,ESP_CACHE_MSYNC_FLAG_DIR_C2M,&stats_.ppaSourceWritebackBytes);
  finished=detail::microseconds();stats_.ppaSourceWritebackMicros+=finished-phase;
  if(!sourceReady)return false;
  ppa_srm_oper_config_t operation={};
  operation.in.buffer=source.pixels;operation.in.pic_w=source.stride;operation.in.pic_h=source.height;
  operation.in.block_w=source.width;operation.in.block_h=source.height;
  operation.in.srm_cm=PPA_SRM_COLOR_MODE_RGB565;
  operation.out.buffer=target.pixels;operation.out.buffer_size=target.bytes;
  operation.out.pic_w=target.stride;operation.out.pic_h=target.height;
  operation.out.block_offset_x=area.x;operation.out.block_offset_y=area.y;
  operation.out.srm_cm=PPA_SRM_COLOR_MODE_RGB565;
  operation.scale_x=scaleX;operation.scale_y=scaleY;operation.mode=PPA_TRANS_MODE_BLOCKING;
  phase=detail::microseconds();++stats_.ppaCalls;
  const auto driverResult=ppa_do_scale_rotate_mirror(static_cast<ppa_client_handle_t>(ppa_),&operation);
  finished=detail::microseconds();stats_.ppaDriverMicros+=finished-phase;
  if(driverResult!=ESP_OK)return false;
  phase=finished;
  const bool coherent=syncCache(target.pixels,target.bytes,ESP_CACHE_MSYNC_FLAG_DIR_M2C,&stats_.ppaTargetInvalidateBytes);
  finished=detail::microseconds();stats_.ppaTargetInvalidateMicros+=finished-phase;
  return coherent;
#else
  (void)source;(void)target;(void)area;return false;
#endif
}
Status NiceGfxPresenter::present(const RenderSurface &source,Viewport area) {
  return presentImpl(source,area,false,0);
}
Status NiceGfxPresenter::presentFit(const RenderSurface &source,uint16_t backgroundColor) {
  if(!display_)return Status::NotInitialized;
  return presentImpl(source,fit(source,display_->width(),display_->height()),true,backgroundColor);
}
Status NiceGfxPresenter::presentImpl(const RenderSurface &source,Viewport area,
                                   bool fillBars,uint16_t backgroundColor) {
  if(!display_||!rows_)return Status::NotInitialized;
  if(!source.valid()||source.stride>SIZE_MAX/source.height/2)return Status::InvalidArgument;
  const size_t sourceBytes=source.stride*source.height*2;
  const uintptr_t sourceStart=reinterpret_cast<uintptr_t>(source.pixels);
  if(sourceStart>UINTPTR_MAX-sourceBytes)return Status::InvalidArgument;
  if(display_->frameOpen())return Status::Busy;
  if(!area.width&&!area.height){area.width=display_->width();area.height=display_->height();}
  if(!area.width||!area.height||area.x<0||area.y<0||area.width>rowWidth_||
      int64_t(area.x)+area.width>display_->width()||int64_t(area.y)+area.height>display_->height())return Status::InvalidArgument;
  const uint64_t start=detail::microseconds();stats_={};
  for(size_t x=0;x<area.width;++x)columns_[x]=uint32_t(uint64_t(x)*source.width/area.width);
  const nice_gfx::DirtyRect dirty=fillBars?
      nice_gfx::DirtyRect(0,0,display_->width(),display_->height()):
      nice_gfx::DirtyRect(area.x,area.y,area.width,area.height);
  // Disjoint bars cover every pixel outside the destination, including odd
  // letterbox widths. Left/right exclude the top/bottom corner rectangles.
  nice_gfx::DirtyRect bars[4];size_t barCount=0;
  if(fillBars) {
    if(area.y>0)bars[barCount++]={0,0,display_->width(),area.y};
    const int32_t bottom=area.y+area.height;
    if(bottom<display_->height())bars[barCount++]={0,bottom,display_->width(),display_->height()-bottom};
    if(area.x>0)bars[barCount++]={0,area.y,area.x,area.height};
    const int32_t right=area.x+area.width;
    if(right<display_->width())bars[barCount++]={right,area.y,display_->width()-right,area.height};
  }
  uint64_t phase=detail::microseconds();stats_.prepareMicros=phase-start;
  auto result=display_->beginFrame(&dirty,1);
  uint64_t finished=detail::microseconds();stats_.beginFrameMicros=finished-phase;
  if(result!=nice_gfx::Status::Ok){stats_.presentMicros=finished-start;return convert(result);}
  nice_gfx::NativeFramebuffer native;
  phase=finished;
  const auto mapped=display_->mapNativeFramebuffer(native);
  finished=detail::microseconds();stats_.mapMicros=finished-phase;
  if(mapped==nice_gfx::Status::Ok) {
    if(!native.pixels||native.stride<native.width||native.width<display_->width()||native.height<display_->height()||
        native.height>SIZE_MAX/native.stride/2||native.bytes<native.stride*native.height*2)result=nice_gfx::Status::Unsupported;
    else {
      const uintptr_t targetStart=reinterpret_cast<uintptr_t>(native.pixels);
      if(targetStart>UINTPTR_MAX-native.bytes ||
          (sourceStart<targetStart+native.bytes && targetStart<sourceStart+sourceBytes)) {
        phase=detail::microseconds();display_->abortFrame();finished=detail::microseconds();
        stats_.abortMicros=finished-phase;stats_.presentMicros=finished-start;return Status::InvalidArgument;
      }
      stats_.mapped=true;
      if(barCount) {
        phase=detail::microseconds();
        for(size_t i=0;i<barCount;++i) {
          const auto &bar=bars[i];
          for(int32_t y=0;y<bar.height;++y) {
            uint16_t *row=native.pixels+size_t(bar.y+y)*native.stride+bar.x;
            if(uint8_t(backgroundColor)==uint8_t(backgroundColor>>8))
              std::memset(row,uint8_t(backgroundColor),size_t(bar.width)*2);
            else std::fill_n(row,size_t(bar.width),backgroundColor);
          }
          ++stats_.barFillCalls;stats_.barPixels+=uint64_t(bar.width)*bar.height;
        }
        finished=detail::microseconds();stats_.barFillMicros+=finished-phase;
      }
      phase=detail::microseconds();
      // Bars are CPU-written BEFORE the unchanged full-target C2M inside PPA.
      // Its destination invalidation covers whole rows, including neighbours.
      stats_.hardwareScaled=scaleWithPpa(source,native,area);
      if(!stats_.hardwareScaled)scaleRows(source,native.pixels+size_t(area.y)*native.stride+area.x,native.stride,area,0,area.height);
      finished=detail::microseconds();stats_.scaleMicros+=finished-phase;
      // Exact mapped source tells nice_gfx to publish/cache-sync this rectangle
      // and retain its normal atomic frame/rotation ownership machinery.
      phase=finished;++stats_.flushCalls;
      result=display_->flush(dirty.x,dirty.y,nice_gfx::PixelView(
          native.pixels+size_t(dirty.y)*native.stride+dirty.x,dirty.width,dirty.height,native.stride),false);
      finished=detail::microseconds();stats_.flushMicros+=finished-phase;
    }
  } else if(mapped==nice_gfx::Status::Unsupported) {
    for(size_t i=0;i<barCount&&result==nice_gfx::Status::Ok;++i) {
      const auto &bar=bars[i];phase=detail::microseconds();++stats_.barFillCalls;
      result=display_->window(bar.x,bar.y,bar.x+bar.width-1,bar.y+bar.height-1);
      if(result==nice_gfx::Status::Ok) {
        result=display_->fill(backgroundColor,uint32_t(bar.width)*bar.height);
        if(result==nice_gfx::Status::Ok)stats_.barPixels+=uint64_t(bar.width)*bar.height;
      }
      finished=detail::microseconds();stats_.barFillMicros+=finished-phase;
    }
    for(size_t y=0;y<area.height&&result==nice_gfx::Status::Ok;y+=16) {
      const size_t count=std::min(size_t(16),size_t(area.height)-y);
      phase=detail::microseconds();
      scaleRows(source,rows_,area.width,area,y,count);
      finished=detail::microseconds();stats_.scaleMicros+=finished-phase;phase=finished;++stats_.flushCalls;
      result=display_->flush(area.x,area.y+int32_t(y),nice_gfx::PixelView(rows_,area.width,count),false);
      finished=detail::microseconds();stats_.flushMicros+=finished-phase;
    }
  } else result=mapped;
  if(result==nice_gfx::Status::Ok){phase=detail::microseconds();result=display_->commitFrame();finished=detail::microseconds();stats_.commitMicros=finished-phase;}
  if(result!=nice_gfx::Status::Ok){phase=detail::microseconds();display_->abortFrame();finished=detail::microseconds();stats_.abortMicros+=finished-phase;}
  stats_.presentMicros=detail::microseconds()-start;
  return convert(result);
}
}
