#pragma once
#include "cansat.hpp"

namespace cansat {
struct Reading {
  bool ok; int16_t temp_centi_c; uint32_t pressure_pa;
};

class Flight {
  Frame frame_{};
  Calibrator calibrator_;
  Mission mission_;
  uint8_t seq_ = 0;
  uint32_t launch_pressure_ = 0;
  bool radio_timeout_ = false;
 public:
  Flight() { frame_.tag = 0xCA; frame_.version = PROTOCOL_VERSION; }
  Frame tick(uint32_t now, Reading reading) {
    frame_.ms = now;
    frame_.seq = seq_++;
    frame_.flags = radio_timeout_ ? RADIO_TIMEOUT : 0;
    frame_.health = radio_timeout_ ? HEALTH_RADIO : 0;
    frame_.event = 0;
    if (reading.ok && reading.pressure_pa) {
      frame_.temp_centi_c = reading.temp_centi_c;
      frame_.pressure_pa = reading.pressure_pa;
      if (mission_.state() == State::PREFLIGHT) calibrator_.add(reading.pressure_pa);
      frame_.relative_altitude_cm = launch_pressure_ ? relative_altitude_cm(reading.pressure_pa, launch_pressure_) : 0;
      frame_.flags |= BMP_OK;
    } else {
      frame_.temp_centi_c = 0;
      frame_.pressure_pa = 0;
      frame_.relative_altitude_cm = 0;
      frame_.health |= HEALTH_BMP;
    }
    frame_.event = static_cast<uint8_t>(mission_.update(now, frame_));
    frame_.state = static_cast<uint8_t>(mission_.state());
    if (mission_.state() == State::READY && !launch_pressure_) launch_pressure_ = calibrator_.baseline_pa();
    seal(frame_);
    return frame_;
  }
  void radio_result(bool sent) { radio_timeout_ = !sent; }
};
} // namespace cansat
