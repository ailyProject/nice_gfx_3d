#pragma once
#include "OrbitControls.h"
#include <Core/I2c.h>
#if NICE_GFX_3D_ESP
#include <Platform/ESP32/BoardI2c.h>
#endif

namespace nice_gfx_3d {

enum class OrbitTouchController : uint8_t { GT1151, GT911 };
struct OrbitTouchConfig {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  OrbitTouchController controller = OrbitTouchController::GT911;
  int sda = 7, scl = 8;
  uint8_t address = 0x5d;
  uint16_t rawWidth = 1024, rawHeight = 600;
#else
  OrbitTouchController controller = OrbitTouchController::GT1151;
  int sda = 0, scl = 1;
  uint8_t address = 0x14;
  uint16_t rawWidth = 800, rawHeight = 480;
#endif
  uint32_t frequency = 400000;
  uint16_t pollIntervalMs = 8, releaseGraceMs = 30;
  uint8_t rotation = 0; // Clockwise quarter turns from the raw panel orientation.
};

// Optional NICE_GFX adapter. Reads both fingers in ONE I2C report, preserves
// controller tracking IDs and never converts two contacts into a single pointer.
// Defaults match the example S31 Korvo-1 and P4 JC1060 boards, not arbitrary boards.
// The portable OrbitControls itself has no dependency on I2C, Arduino or NICE_GFX.
class NiceGfxOrbitInput {
 public:
#if NICE_GFX_3D_ESP
  explicit NiceGfxOrbitInput(const OrbitTouchConfig &config = OrbitTouchConfig())
      : config_(config), boardBus_(config.sda, config.scl, config.frequency), bus_(&boardBus_) {}
  NiceGfxOrbitInput(nice_gfx::I2cBus &bus, const OrbitTouchConfig &config)
      : config_(config), boardBus_(config.sda, config.scl, config.frequency), bus_(&bus) {}
#else
  explicit NiceGfxOrbitInput(nice_gfx::I2cBus &bus, const OrbitTouchConfig &config = OrbitTouchConfig())
      : config_(config), bus_(&bus) {}
#endif
  NiceGfxOrbitInput(const NiceGfxOrbitInput &) = delete;
  NiceGfxOrbitInput &operator=(const NiceGfxOrbitInput &) = delete;

  bool begin(uint16_t width, uint16_t height) {
    ready_ = false; active_ = false; polled_ = false; gap_ = false; lastCount_ = 0;
    retryInterval_ = config_.pollIntervalMs;
    if (!width || !height || config_.rawWidth < 2 || config_.rawHeight < 2 ||
        config_.rotation > 3 || !config_.pollIntervalMs || !bus_ ||
        (config_.controller == OrbitTouchController::GT1151 && config_.address != 0x14) ||
        (config_.controller == OrbitTouchController::GT911 && config_.address != 0x14 && config_.address != 0x5d)) {
      status_ = nice_gfx::Status::InvalidArgument; return false;
    }
    width_ = width; height_ = height;
    status_ = bus_->begin(config_.address);
    if (status_ != nice_gfx::Status::Ok) return false;
    // Probe the product-ID register without consuming or acknowledging a touch.
    uint8_t identity[4];
    ready_ = readRegister(0x8140, identity, sizeof(identity));
    return ready_;
  }
  nice_gfx::Status status() const { return status_; }
  bool ready() const { return ready_; }
  uint32_t polls() const { return polls_; }
  uint32_t errors() const { return errors_; }
  uint32_t contactFrames() const { return contactFrames_; }
  uint8_t lastContactCount() const { return lastCount_; }

  void update(OrbitControls &controls, uint32_t now) {
    if (!ready_) return;
    // A new session must not reuse the caller's old gesture, even if GT911
    // has no fresh report yet. begin() also resets a previous error backoff.
    if (!polled_) controls.cancel();
    if (polled_ && uint32_t(now - lastPoll_) < retryInterval_) return;
    polled_ = true; lastPoll_ = now; ++polls_;
    controls.setViewport(width_, height_);
    OrbitContact contacts[2]; uint8_t count = 0;
    const Result result = acquire(contacts, count);
    if (result == Result::Error) {
      ++errors_; retryInterval_ = 500; active_ = false; gap_ = false; lastCount_ = 0;
      controls.cancel(); return;
    }
    retryInterval_ = config_.pollIntervalMs;
    if (result == Result::Contacts) {
      // GT1151 release grace preserves routing, not a stale position baseline:
      // a quick lift/re-touch may reuse the controller's same tracking ID.
      if (gap_) controls.cancel();
      gap_ = false;
      ++contactFrames_; lastContact_ = now; active_ = true; lastCount_ = count;
      controls.setContacts(contacts, count);
    } else if (result == Result::Release && active_) {
      gap_ = true;
      if (config_.controller == OrbitTouchController::GT911 || uint32_t(now - lastContact_) >= config_.releaseGraceMs) {
        controls.setContacts(nullptr, 0); active_ = false; gap_ = false; lastCount_ = 0;
      }
    }
  }

 private:
  enum class Result { Contacts, Release, Unchanged, Error };
  OrbitTouchConfig config_;
#if NICE_GFX_3D_ESP
  nice_gfx::detail::BoardI2c boardBus_;
#endif
  nice_gfx::I2cBus *bus_ = nullptr;
  nice_gfx::Status status_ = nice_gfx::Status::NotInitialized;
  bool gap_ = false; // Uses the padding before the uint16_t dimensions.
  uint16_t width_ = 0, height_ = 0, retryInterval_ = 8;
  uint32_t lastPoll_ = 0, lastContact_ = 0, polls_ = 0, errors_ = 0, contactFrames_ = 0;
  uint8_t lastCount_ = 0;
  bool ready_ = false, active_ = false, polled_ = false;
  bool readRegister(uint16_t reg, uint8_t *data, size_t size) {
    const uint8_t address[] = {uint8_t(reg >> 8), uint8_t(reg)};
    status_ = bus_->transfer(config_.address, address, 2, data, size);
    return status_ == nice_gfx::Status::Ok;
  }
  bool acknowledge() {
    const uint8_t command[] = {0x81, 0x4e, 0};
    status_ = bus_->transfer(config_.address, command, sizeof(command), nullptr, 0);
    return status_ == nice_gfx::Status::Ok;
  }
  Result malformed() { status_ = nice_gfx::Status::IoError; return Result::Error; }
  bool contact(OrbitContact &out, const uint8_t *p) {
    uint16_t x = uint16_t(p[1] | uint16_t(p[2]) << 8), y = uint16_t(p[3] | uint16_t(p[4]) << 8);
    if (x >= config_.rawWidth || y >= config_.rawHeight) return false;
    uint16_t w = config_.rawWidth, h = config_.rawHeight;
    switch (config_.rotation) {
      case 1: { const uint16_t oldX=x; x=uint16_t(h-1-y); y=oldX; std::swap(w,h); break; }
      case 2: x=uint16_t(w-1-x); y=uint16_t(h-1-y); break;
      case 3: { const uint16_t oldX=x; x=y; y=uint16_t(w-1-oldX); std::swap(w,h); break; }
      default: break;
    }
    out.id=p[0]&15;
    out.x=float(x)*float(width_-1)/float(w-1);
    out.y=float(y)*float(height_-1)/float(h-1);
    return true;
  }
  Result acquire(OrbitContact *out, uint8_t &selected) {
    uint8_t flags;
    if (!readRegister(0x814e, &flags, 1)) return Result::Error;
    const bool gt1151 = config_.controller == OrbitTouchController::GT1151;
    if (!gt1151 && !(flags & 0x80)) return Result::Unchanged; // GT911 held state.
    const uint8_t count = flags & 15, maximum = gt1151 ? 10 : 5;
    uint8_t report[83] = {};
    const size_t bytes = gt1151 ? 1 + size_t(count)*8 + 2 : size_t(count)*8;
    const bool valid = count <= maximum;
    const bool received = valid && count && readRegister(gt1151 ? 0x814e : 0x814f, report, bytes);
    const nice_gfx::Status readStatus = status_;
    if (!acknowledge()) return Result::Error;
    if (!valid) return malformed();
    if (!count) return Result::Release;
    if (!received) { status_ = readStatus; return Result::Error; }
    if (gt1151) {
      uint8_t sum = 0; for (size_t i=0;i<bytes;++i) sum=uint8_t(sum+report[i]);
      if (sum || (report[0]&15)!=count) return malformed();
    }
    uint16_t ids = 0; selected = 0;
    for (uint8_t i=0;i<count;++i) {
      OrbitContact point;
      if (!contact(point, report + (gt1151 ? 1 : 0) + size_t(i)*8) || (ids & uint16_t(1u << point.id))) return malformed();
      ids |= uint16_t(1u << point.id);
      // Stable lowest tracking IDs when a third finger touches the screen.
      if (!selected) { out[0]=point; selected=1; }
      else if (selected==1) { out[1]=point; selected=2; if(out[1].id<out[0].id)std::swap(out[0],out[1]); }
      else if(point.id<out[1].id) { out[1]=point; if(out[1].id<out[0].id)std::swap(out[0],out[1]); }
    }
    return Result::Contacts;
  }
};
} // namespace nice_gfx_3d
