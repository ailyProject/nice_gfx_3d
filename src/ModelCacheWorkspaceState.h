#pragma once
#include "ModelCacheWorkspace.h"
#include "Memory.h"
#include <array>
#include <cstdint>

namespace nice_gfx_3d { namespace detail {
enum class ModelCacheKind:uint8_t {Maps,Positions,NormalColors,RigidJoints,TextureBlocks,TexturePixels};
struct ModelCacheSlot {
  ModelCacheKind kind=ModelCacheKind::Maps;
  size_t index=0,bytes=0,alignment=0;
  MemoryPreference preference=MemoryPreference::InternalFirst;
  void* pointer=nullptr;
  bool leased=false,owned=false;
};
struct ModelCacheState {
  static constexpr size_t MaxBlocks=32;
  std::array<ModelCacheSlot,MaxBlocks> slots{};
  size_t count=0,active=0;
  const void* owner=nullptr;
  bool staticFrozen=false,textureFrozen=false;
  Status lastStatus=Status::Ok;
  ~ModelCacheState(){freeUnleased();}
  static std::shared_ptr<ModelCacheState> create();
  void* acquire(const void* model,ModelCacheKind kind,size_t index,size_t bytes,
                MemoryPreference preference,size_t alignment);
  bool addBorrowed(const void* model,ModelCacheKind kind,size_t index,void* pointer,
                   size_t bytes,MemoryPreference preference,size_t alignment);
  void ownBorrowed();
  void release(void* pointer);
  void rollbackSince(size_t first);
  void pruneUnleasedSince(size_t first);
  void freeUnleased();
  bool frozen(ModelCacheKind kind)const{return kind==ModelCacheKind::TextureBlocks||kind==ModelCacheKind::TexturePixels?textureFrozen:staticFrozen;}
  size_t reservedBytes()const;
  size_t leasedBytes()const;
};
struct ModelCacheDeleter {
  ModelCacheState* state=nullptr;
  void operator()(void* pointer)const{if(state)state->release(pointer);else detail::release(pointer);}
};
struct ModelCacheTransaction {
  std::shared_ptr<ModelCacheState> state;
  size_t first=0;
  bool committed=false;
  explicit ModelCacheTransaction(std::shared_ptr<ModelCacheState> value):state(std::move(value)),first(state?state->count:0){}
  ~ModelCacheTransaction(){if(state&&!committed)state->rollbackSince(first);}
  void commit(bool texture){
    if(state){state->pruneUnleasedSince(first);if(texture)state->textureFrozen=true;else state->staticFrozen=true;}
    committed=true;
  }
};
}}
