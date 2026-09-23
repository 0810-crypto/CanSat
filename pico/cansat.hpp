#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace cansat {
constexpr uint8_t PROTOCOL_VERSION = 3;
#pragma pack(push, 1)
struct Frame {
  uint8_t tag, version, seq, state;
  uint32_t ms;
  int16_t temp_centi_c;
  uint32_t pressure_pa;
  int32_t relative_altitude_cm;
  uint8_t flags;
  uint8_t health, event;
  uint16_t crc;
};
#pragma pack(pop)
static_assert(sizeof(Frame) == 23);

inline uint16_t crc16(const uint8_t *p, unsigned n) {
  uint16_t c = 0;
  while (n--) { c ^= uint16_t(*p++) << 8; for (uint8_t i = 0; i < 8; ++i) c = c & 0x8000 ? uint16_t(c << 1) ^ 0x1021 : uint16_t(c << 1); }
  return c;
}
inline void seal(Frame &f) { f.crc = crc16(reinterpret_cast<const uint8_t *>(&f), sizeof(f) - 2); }
inline bool valid(const Frame &f) { return f.tag == 0xCA && f.version == PROTOCOL_VERSION && f.crc == crc16(reinterpret_cast<const uint8_t *>(&f), sizeof(f) - 2); }
constexpr const char *CSV_HEADER = "ms,seq,state,temp_centi_c,pressure_pa,relative_altitude_cm,flags,health,event";
inline void write_csv(FILE *out, const Frame &f) {
  std::fprintf(out, "%lu,%u,%u,%d,%lu,%ld,%u,%u,%u\n", static_cast<unsigned long>(f.ms), f.seq, f.state, f.temp_centi_c, static_cast<unsigned long>(f.pressure_pa), static_cast<long>(f.relative_altitude_cm), f.flags, f.health, f.event);
}

constexpr uint8_t BMP_OK = 1, RADIO_TIMEOUT = 2;
constexpr uint8_t HEALTH_BMP = 1, HEALTH_RADIO = 2;
enum class State : uint8_t { BOOT, PREFLIGHT, READY, ASCENT, DESCENT, LANDED, RECOVERY };
enum class Event : uint8_t { NONE, BOOTED, READY, ASCENT, DESCENT, LANDED, RECOVERY, COMMAND };
inline int32_t relative_altitude_cm(uint32_t pressure_pa, uint32_t launch_pressure_pa) {
  if (!pressure_pa || !launch_pressure_pa) return 0;
  return static_cast<int32_t>(4'433'000.f * (1.f - std::pow(pressure_pa / float(launch_pressure_pa), .19029496f)));
}
struct Calibrator {
  uint64_t total = 0; uint16_t samples = 0;
  void add(uint32_t pressure_pa) { total += pressure_pa; ++samples; }
  uint32_t baseline_pa() const { return samples ? uint32_t(total / samples) : 0; }
};
class Mission {
  State state_ = State::BOOT; uint32_t stable_since_ = 0, landed_since_ = 0; int32_t peak_cm_ = 0;
 public:
  State state() const { return state_; }
  Event update(uint32_t now, const Frame &f) {
    bool bmp = f.flags & BMP_OK;
    if (state_ == State::BOOT) { state_ = State::PREFLIGHT; stable_since_ = now; return Event::BOOTED; }
    if (state_ == State::PREFLIGHT) { if (!bmp) stable_since_ = now; else if (now - stable_since_ >= 5000) { state_ = State::READY; return Event::READY; } return Event::NONE; }
    if (!bmp) return Event::NONE;
    if (state_ == State::READY && f.relative_altitude_cm >= 1000) { state_ = State::ASCENT; peak_cm_ = f.relative_altitude_cm; return Event::ASCENT; }
    if (state_ == State::ASCENT) { if (f.relative_altitude_cm > peak_cm_) peak_cm_ = f.relative_altitude_cm; if (peak_cm_ >= f.relative_altitude_cm + 300) { state_ = State::DESCENT; return Event::DESCENT; } }
    // ponytail: barometric landing is coarse; add motion sensing if precision matters.
    if (state_ == State::DESCENT) { if (f.relative_altitude_cm <= 300) { if (!landed_since_) landed_since_ = now; if (now - landed_since_ >= 3000) { state_ = State::LANDED; return Event::LANDED; } } else landed_since_ = 0; }
    if (state_ == State::LANDED && now - landed_since_ >= 30000) { state_ = State::RECOVERY; return Event::RECOVERY; }
    return Event::NONE;
  }
};
} // namespace cansat
