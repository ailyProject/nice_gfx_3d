#include "ModelCacheWorkspaceState.h"
#include <new>
#include <cstdlib>
#if NICE_GFX_3D_ESP
#include "esp_heap_caps.h"
#endif

namespace nice_gfx_3d { namespace detail {
std::shared_ptr<ModelCacheState> ModelCacheState::create(){
#if defined(__cpp_exceptions)||defined(__EXCEPTIONS)
  try{return std::shared_ptr<ModelCacheState>(new(std::nothrow) ModelCacheState());}
  catch(const std::bad_alloc&){return {};}
#else
  return std::shared_ptr<ModelCacheState>(new(std::nothrow) ModelCacheState());
#endif
}
void* ModelCacheState::acquire(const void* model,ModelCacheKind kind,size_t index,size_t bytes,
    MemoryPreference preference,size_t alignment){
  if(!bytes){lastStatus=Status::InvalidArgument;return nullptr;}
  if(active&&owner!=model){lastStatus=Status::Busy;return nullptr;}
  for(size_t i=0;i<count;++i){auto& slot=slots[i];if(slot.kind!=kind||slot.index!=index)continue;
    if(slot.leased){lastStatus=Status::Busy;return nullptr;}
    if(slot.bytes!=bytes||slot.alignment!=alignment||slot.preference!=preference){lastStatus=Status::CapacityExceeded;return nullptr;}
    slot.leased=true;++active;owner=model;lastStatus=Status::Ok;return slot.pointer;
  }
  if(frozen(kind)||count==MaxBlocks){lastStatus=Status::CapacityExceeded;return nullptr;}
  void* pointer=nullptr;
#if NICE_GFX_3D_ESP
  if(kind==ModelCacheKind::TexturePixels)pointer=heap_caps_aligned_alloc(alignment,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  else
#endif
    pointer=detail::allocate(bytes,preference,alignment);
  if(!pointer){lastStatus=Status::NoMemory;return nullptr;}
  slots[count++]={kind,index,bytes,alignment,preference,pointer,true,true};
  ++active;owner=model;lastStatus=Status::Ok;return pointer;
}
bool ModelCacheState::addBorrowed(const void* model,ModelCacheKind kind,size_t index,void* pointer,
    size_t bytes,MemoryPreference preference,size_t alignment){
  if(!pointer)return true;
  if(count==MaxBlocks||(active&&owner!=model)){lastStatus=Status::CapacityExceeded;return false;}
  for(size_t i=0;i<count;++i)if(slots[i].pointer==pointer){lastStatus=Status::InvalidArgument;return false;}
  slots[count++]={kind,index,bytes,alignment,preference,pointer,true,false};++active;owner=model;
  return true;
}
void ModelCacheState::ownBorrowed(){for(size_t i=0;i<count;++i)slots[i].owned=true;}
void ModelCacheState::release(void* pointer){
  if(!pointer)return;
  for(size_t i=0;i<count;++i)if(slots[i].pointer==pointer){
    if(!slots[i].leased)std::abort();
    slots[i].leased=false;if(!--active)owner=nullptr;return;
  }
  std::abort();
}
void ModelCacheState::pruneUnleasedSince(size_t first){
  for(size_t i=first;i<count;){
    if(slots[i].leased){++i;continue;}
    if(slots[i].owned)detail::release(slots[i].pointer);
    for(size_t next=i+1;next<count;++next)slots[next-1]=slots[next];
    slots[--count]={};
  }
}
void ModelCacheState::rollbackSince(size_t first){
  for(size_t i=first;i<count;++i)if(slots[i].leased)std::abort();
  pruneUnleasedSince(first);
}
void ModelCacheState::freeUnleased(){
  // A live Model holds a shared State until all of its cache owners release.
  if(active)std::abort();
  for(size_t i=0;i<count;++i)if(slots[i].owned)detail::release(slots[i].pointer);
  count=0;owner=nullptr;staticFrozen=textureFrozen=false;
}
size_t ModelCacheState::reservedBytes()const{size_t bytes=0;for(size_t i=0;i<count;++i)bytes+=slots[i].bytes;return bytes;}
size_t ModelCacheState::leasedBytes()const{size_t bytes=0;for(size_t i=0;i<count;++i)if(slots[i].leased)bytes+=slots[i].bytes;return bytes;}
}}
namespace nice_gfx_3d {
ModelCacheWorkspace::~ModelCacheWorkspace()=default;
Status ModelCacheWorkspace::end(){if(state_&&state_->active)return Status::Busy;if(state_)state_->freeUnleased();state_.reset();return Status::Ok;}
size_t ModelCacheWorkspace::reservedBytes()const{return state_?state_->reservedBytes():0;}
size_t ModelCacheWorkspace::leasedBytes()const{return state_?state_->leasedBytes():0;}
size_t ModelCacheWorkspace::reservedBlocks()const{return state_?state_->count:0;}
size_t ModelCacheWorkspace::leasedBlocks()const{return state_?state_->active:0;}
}
