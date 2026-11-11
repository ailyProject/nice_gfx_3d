#pragma once
#include <cstddef>
#include <cstdint>

namespace nice_gfx_3d {
namespace detail {
namespace rgb565_tint {

template<size_t Levels>
struct ChannelTable {
  uint8_t values[256][Levels] = {};
};

template<size_t Levels>
constexpr ChannelTable<Levels> makeChannelTable() {
  ChannelTable<Levels> table;
  for(unsigned strength=0;strength<256;++strength) {
    for(unsigned component=0;component<Levels;++component) {
      table.values[strength][component]=uint8_t((component*strength+127u)/255u);
    }
  }
  return table;
}

// Constant initialization keeps these tables in read-only flash storage.
// Select the three rows once per triangle, outside its fragment loop.
alignas(64) inline constexpr auto table5Bit=makeChannelTable<32>();
alignas(64) inline constexpr auto table6Bit=makeChannelTable<64>();
static_assert(sizeof(table5Bit)==8192,"Unexpected five-bit tint table size");
static_assert(sizeof(table6Bit)==16384,"Unexpected six-bit tint table size");

struct Rows {
  const uint8_t* red5;
  const uint8_t* green6;
  const uint8_t* blue5;
};

constexpr Rows selectRows(uint8_t red,uint8_t green,uint8_t blue) {
  return {table5Bit.values[red],table6Bit.values[green],table5Bit.values[blue]};
}

constexpr uint16_t sample(uint16_t color,const uint8_t* red5,
    const uint8_t* green6,const uint8_t* blue5) {
  return uint16_t((uint16_t(red5[color>>11])<<11)|
    (uint16_t(green6[(color>>5)&63])<<5)|uint16_t(blue5[color&31]));
}

constexpr uint16_t sample(uint16_t color,const Rows& rows) {
  return sample(color,rows.red5,rows.green6,rows.blue5);
}

static_assert(sample(0xffff,selectRows(255,255,255))==0xffff,"White tint must preserve RGB565");
static_assert(sample(0xffff,selectRows(0,0,0))==0,"Zero tint must produce black");

} // namespace rgb565_tint
} // namespace detail
} // namespace nice_gfx_3d
