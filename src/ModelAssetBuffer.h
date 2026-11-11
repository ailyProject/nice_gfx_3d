#pragma once
#include "Config.h"
#include "Memory.h"
#include "Status.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#if NICE_GFX_3D_ESP
#include <esp_heap_caps.h>
#include <miniz.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6,0,0)
#include <mbedtls/sha256.h>
#else
#include <psa/crypto.h>
#endif
#define NICE_GFX_3D_ASSET_HAS_MINIZ 1
#elif __has_include(<miniz.h>)
#include <miniz.h>
#define NICE_GFX_3D_ASSET_HAS_MINIZ 1
#else
#define NICE_GFX_3D_ASSET_HAS_MINIZ 0
#endif

#define NICE_GFX_3D_HAS_MODEL_ASSET_BUFFER 1
namespace nice_gfx_3d {
enum class ModelAssetCodec:uint8_t {Raw=0,Zlib=1,ChannelDeltaBytePlanesZlib=2};
struct ModelAssetDescriptor {
  const uint8_t* packed=nullptr;
  size_t packedBytes=0,rawBytes=0;
  ModelAssetCodec codec=ModelAssetCodec::Raw;
  uint8_t digest[32]={}; // SHA256 of the restored, uncompressed NG3D bytes.
};
namespace model_asset_detail {
inline uint32_t read32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void write32(uint8_t* p,uint32_t value){p[0]=uint8_t(value);p[1]=uint8_t(value>>8);p[2]=uint8_t(value>>16);p[3]=uint8_t(value>>24);}
inline void undoBytePlanes(const uint8_t* input,uint8_t* output,size_t bytes){
  const size_t words=bytes/4;
  for(size_t plane=0;plane<4;++plane)for(size_t word=0;word<words;++word)output[word*4+plane]=input[plane*words+word];
  if(bytes%4)std::memcpy(output+words*4,input+words*4,bytes%4);
}
inline bool undoChannelPrediction(uint8_t* bytes,size_t length){
  if(length<32||std::memcmp(bytes,"NG3D",4)||bytes[4]!=1||bytes[5]||bytes[6]!=32||bytes[7]||read32(bytes+8)!=length||read32(bytes+12)!=12)return false;
  const size_t directory=read32(bytes+16);
  if(directory<32||directory%4||directory>length||12*16>length-directory||read32(bytes+20)||read32(bytes+24)||read32(bytes+28))return false;
  constexpr const char* tags[]={"VERT","VSKN","INDX","MESH","PRIM","NODE","MATL","TEXR","SKIN","ANIM","CHAN","ROOT"};
  constexpr size_t strides[]={36,24,4,8,32,60,28,24,24,16,24,4};
  size_t offsets[12]={},counts[12]={},fixedEnd=directory+12*16;uint16_t seen=0;
  for(size_t i=0;i<12;++i){
    const uint8_t* record=bytes+directory+i*16;size_t id=0;
    while(id<12&&std::memcmp(record,tags[id],4))++id;
    if(id==12||(seen&(1u<<id)))return false;seen|=uint16_t(1u<<id);
    const size_t count=read32(record+4),stride=read32(record+8),offset=read32(record+12);
    if(stride!=strides[id]||offset<directory+12*16||offset%4||offset>length||count>(length-offset)/stride)return false;
    offsets[id]=offset;counts[id]=count;const size_t end=offset+count*stride;if(end>fixedEnd)fixedEnd=end;
  }
  for(size_t i=0;i<12;++i)if(counts[i])for(size_t j=0;j<i;++j)
    if(counts[j]&&offsets[i]<offsets[j]+counts[j]*strides[j]&&offsets[j]<offsets[i]+counts[i]*strides[i])return false;
  const size_t channelOffset=offsets[10],channelCount=counts[10];
  // Validate every channel before changing payload. Restored arrays cannot
  // overwrite the directory/records read by subsequent iterations.
  for(size_t i=0;i<channelCount;++i){
    const uint8_t* record=bytes+channelOffset+i*24;
    const uint32_t property=read32(record+4),interpolation=read32(record+8);
    const size_t keys=read32(record+12),times=read32(record+16),values=read32(record+20),components=property==1?4:3;
    if(property>2||interpolation>1||!keys||times<fixedEnd||values<fixedEnd||times%4||values%4||times>length||values>length||keys>(length-times)/4||keys>(length-values)/(components*4))return false;
  }
  const auto restore=[&](size_t offset,size_t keys,size_t components){
    for(size_t i=components;i<keys*components;++i){
      const uint32_t encoded=read32(bytes+offset+i*4),delta=(encoded>>1)^(0u-(encoded&1u));
      write32(bytes+offset+i*4,read32(bytes+offset+(i-components)*4)+delta);
    }
  };
  for(size_t i=0;i<channelCount;++i){const uint8_t* record=bytes+channelOffset+i*24;const size_t keys=read32(record+12);restore(read32(record+16),keys,1);restore(read32(record+20),keys,read32(record+4)==1?4:3);}
  return true;
}

#if !NICE_GFX_3D_ESP
// Portable authoring fallback. Device builds use their SDK SHA backend below.
inline void sha256(const uint8_t* bytes,size_t length,uint8_t* digest){
  constexpr uint32_t constants[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t hash[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  const auto rotate=[](uint32_t v,unsigned n){return (v>>n)|(v<<(32-n));};
  const auto block=[&](const uint8_t* data){
    uint32_t words[64];for(size_t i=0;i<16;++i)words[i]=(uint32_t(data[i*4])<<24)|(uint32_t(data[i*4+1])<<16)|(uint32_t(data[i*4+2])<<8)|data[i*4+3];
    for(size_t i=16;i<64;++i){const uint32_t x=words[i-15],y=words[i-2];words[i]=words[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+words[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10));}
    uint32_t a=hash[0],b=hash[1],c=hash[2],d=hash[3],e=hash[4],f=hash[5],g=hash[6],h=hash[7];
    for(size_t i=0;i<64;++i){const uint32_t first=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+words[i],second=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+first;d=c;c=b;b=a;a=first+second;}
    hash[0]+=a;hash[1]+=b;hash[2]+=c;hash[3]+=d;hash[4]+=e;hash[5]+=f;hash[6]+=g;hash[7]+=h;
  };
  size_t offset=0;while(length-offset>=64){block(bytes+offset);offset+=64;}
  uint8_t tail[128]={};const size_t remaining=length-offset;if(remaining)std::memcpy(tail,bytes+offset,remaining);tail[remaining]=0x80;
  const size_t padded=remaining<56?64:128;const uint64_t bits=uint64_t(length)*8;
  for(size_t i=0;i<8;++i)tail[padded-1-i]=uint8_t(bits>>(i*8));block(tail);if(padded==128)block(tail+64);
  for(size_t i=0;i<8;++i){digest[i*4]=uint8_t(hash[i]>>24);digest[i*4+1]=uint8_t(hash[i]>>16);digest[i*4+2]=uint8_t(hash[i]>>8);digest[i*4+3]=uint8_t(hash[i]);}
}
#endif
inline bool sha256Matches(const uint8_t* bytes,size_t length,const uint8_t* expected){
  uint8_t digest[32]={};
#if NICE_GFX_3D_ESP
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6,0,0)
  mbedtls_sha256_context context;mbedtls_sha256_init(&context);
  int result=mbedtls_sha256_starts(&context,0);alignas(64) uint8_t chunk[1024];
  for(size_t offset=0;result==0&&offset<length;){const size_t count=length-offset<sizeof(chunk)?length-offset:sizeof(chunk);std::memcpy(chunk,bytes+offset,count);result=mbedtls_sha256_update(&context,chunk,count);offset+=count;}
  if(!result)result=mbedtls_sha256_finish(&context,digest);mbedtls_sha256_free(&context);if(result)return false;
#else
  size_t count=0;if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_compute(PSA_ALG_SHA_256,bytes,length,digest,sizeof(digest),&count)!=PSA_SUCCESS||count!=sizeof(digest))return false;
#endif
#else
  sha256(bytes,length,digest);
#endif
  return std::memcmp(digest,expected,sizeof(digest))==0;
}
inline uint8_t* allocate(size_t bytes){
#if NICE_GFX_3D_ESP
  return static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(detail::allocate(bytes,MemoryPreference::ExternalFirst));
#endif
}
}

// Setup-only temporary storage. Model::load deep-copies the decoded bytes, so
// clear() may run immediately after it returns and every ModelView is finished.
// Raw external input is borrowed and must outlive this buffer's readers. Raw
// input aliasing this buffer's own allocation retains that allocation safely.
// Failure preserves data()/size()/ownership; status()/error() describe the attempt.
// Device inflate/hash use SDK facilities; host Raw works without dependencies,
// while host compressed codecs require a miniz.h implementation at link time.
class ModelAssetBuffer {
 public:
  static constexpr size_t MaximumBytes=8u*1024u*1024u;
  ModelAssetBuffer()=default;
  ~ModelAssetBuffer(){clear();}
  ModelAssetBuffer(const ModelAssetBuffer&)=delete;ModelAssetBuffer& operator=(const ModelAssetBuffer&)=delete;
  ModelAssetBuffer(ModelAssetBuffer&&)=delete;ModelAssetBuffer& operator=(ModelAssetBuffer&&)=delete;
  Status decode(const ModelAssetDescriptor& source,size_t maximumBytes=MaximumBytes){
    const size_t limit=maximumBytes<MaximumBytes?maximumBytes:MaximumBytes;
    if(!source.packed||!source.packedBytes||!source.rawBytes||uint8_t(source.codec)>2||reinterpret_cast<uintptr_t>(source.packed)>UINTPTR_MAX-source.packedBytes)return fail(Status::InvalidArgument,"invalid asset descriptor");
    if(source.packedBytes>limit||source.rawBytes>limit)return fail(Status::CapacityExceeded,"asset byte budget exceeded");
    if(source.codec==ModelAssetCodec::Raw){
      if(source.packedBytes!=source.rawBytes)return fail(Status::InvalidData,"raw asset length differs");
      bool selfAlias=false;
      if(owned_){const uintptr_t first=reinterpret_cast<uintptr_t>(source.packed),base=reinterpret_cast<uintptr_t>(owned_);
        if(first<base+allocationBytes_&&base<first+source.rawBytes){if(first<base||source.rawBytes>allocationBytes_-(first-base))return fail(Status::InvalidArgument,"raw asset partially overlaps owned storage");selfAlias=true;}}
      if(!model_asset_detail::sha256Matches(source.packed,source.rawBytes,source.digest))return fail(Status::InvalidData,"restored asset SHA256 differs");
      if(!selfAlias){detail::release(owned_);owned_=nullptr;allocationBytes_=0;}
      data_=source.packed;size_=source.rawBytes;status_=Status::Ok;error_="ok";return status_;
    }
#if NICE_GFX_3D_ASSET_HAS_MINIZ
    uint8_t* inflated=model_asset_detail::allocate(source.rawBytes);
    if(!inflated)return fail(Status::NoMemory,"not enough external memory for asset");
    const size_t decoded=tinfl_decompress_mem_to_mem(inflated,source.rawBytes,source.packed,source.packedBytes,TINFL_FLAG_PARSE_ZLIB_HEADER);
    if(decoded!=source.rawBytes){detail::release(inflated);return fail(Status::InvalidData,"asset inflate size or checksum differs");}
    uint8_t* raw=inflated;
    if(source.codec==ModelAssetCodec::ChannelDeltaBytePlanesZlib){
      raw=model_asset_detail::allocate(source.rawBytes);
      if(!raw){detail::release(inflated);return fail(Status::NoMemory,"not enough external memory to restore asset");}
      model_asset_detail::undoBytePlanes(inflated,raw,source.rawBytes);detail::release(inflated);
      if(!model_asset_detail::undoChannelPrediction(raw,source.rawBytes)){detail::release(raw);return fail(Status::InvalidData,"invalid channel prediction metadata");}
    }
    if(!model_asset_detail::sha256Matches(raw,source.rawBytes,source.digest)){detail::release(raw);return fail(Status::InvalidData,"restored asset SHA256 differs");}
    detail::release(owned_);owned_=raw;allocationBytes_=source.rawBytes;data_=raw;size_=source.rawBytes;status_=Status::Ok;error_="ok";return status_;
#else
    return fail(Status::Unsupported,"compressed host assets require miniz");
#endif
  }
  const uint8_t* data()const{return data_;}
  size_t size()const{return size_;}
  bool owned()const{return owned_!=nullptr;}
  Status status()const{return status_;}
  const char* error()const{return error_;}
  void clear(){detail::release(owned_);owned_=nullptr;data_=nullptr;size_=allocationBytes_=0;status_=Status::NotInitialized;error_="asset is not initialized";}
 private:
  const uint8_t* data_=nullptr;
  uint8_t* owned_=nullptr;
  size_t size_=0,allocationBytes_=0;
  Status status_=Status::NotInitialized;
  const char* error_="asset is not initialized";
  Status fail(Status status,const char* error){status_=status;error_=error;return status_;}
};
}
