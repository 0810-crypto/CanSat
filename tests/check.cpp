#include "flight.hpp"
#include <cassert>

int main() {
  const uint8_t crc_input[] = {'1','2','3','4','5','6','7','8','9'};
  assert(cansat::crc16(crc_input, sizeof crc_input) == 0x31C3);
  cansat::Frame f{0xCA, cansat::PROTOCOL_VERSION, 7, 0, 1234, 2145, 100123, 0, 1, 0, 0, 0};
  cansat::seal(f);
  assert(cansat::valid(f));
  ++f.flags;
  assert(!cansat::valid(f));

  // Would catch reporting absolute sea-level height instead of launch-relative height.
  assert(cansat::relative_altitude_cm(101325, 101325) == 0);
  int32_t height = cansat::relative_altitude_cm(100129, 101325);
  assert(height > 9'500 && height < 10'500);

  // Would catch a preflight baseline based on one noisy pressure sample.
  cansat::Calibrator c;
  c.add(100000); c.add(100100); c.add(99900);
  assert(c.baseline_pa() == 100000);

  // Would catch the mission entering ascent before a healthy preflight completes.
  cansat::Mission mission;
  cansat::Frame healthy{0xCA, cansat::PROTOCOL_VERSION, 0, 0, 0, 2000, 100000, 0, cansat::BMP_OK, 0, 0, 0};
  assert(mission.update(0, healthy) == cansat::Event::BOOTED);
  assert(mission.state() == cansat::State::PREFLIGHT);
  assert(mission.update(5'100, healthy) == cansat::Event::READY);
  healthy.relative_altitude_cm = 1'500;
  assert(mission.update(5'200, healthy) == cansat::Event::ASCENT);
  healthy.flags = 0;
  healthy.relative_altitude_cm = 0;
  assert(mission.update(5'300, healthy) == cansat::Event::NONE);
  assert(mission.state() == cansat::State::ASCENT);

  cansat::Flight flight;
  auto first = flight.tick(0, {true, 2000, 101325});
  assert(cansat::valid(first) && (first.flags & cansat::BMP_OK));
  flight.radio_result(false);
  auto failed = flight.tick(100, {false, 0, 0});
  assert(cansat::valid(failed) && (failed.flags & cansat::RADIO_TIMEOUT));
  assert((failed.health & (cansat::HEALTH_BMP | cansat::HEALTH_RADIO)) == (cansat::HEALTH_BMP | cansat::HEALTH_RADIO));
  assert(failed.pressure_pa == 0);

}
