#include "flight.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
  if (argc != 5) return 2;
  const int peak_cm = std::atoi(argv[1]), ambient_centi = std::atoi(argv[2]), base_pressure = std::atoi(argv[3]);
  const char *fault = argv[4];
  const uint8_t crc_example[] = {'1','2','3','4','5','6','7','8','9'};
  if (cansat::crc16(crc_example, sizeof crc_example) != 0x31C3) return std::fprintf(stderr, "CRC reference check failed\n"), 1;
  cansat::Flight flight;
  std::puts("t_ms,physical_alt_cm,sensor_temp_centi,sensor_pressure_pa,bmp_input_ok,frame_seq,frame_state,frame_temp_centi,frame_pressure_pa,frame_alt_cm,frame_flags,frame_health,frame_event,tx_ok,rx_ok");
  for (uint32_t now = 0; now <= 75'000; now += 100) {
    int32_t altitude = now < 12'000 ? 0 : now < 25'000 ? int32_t((now - 12'000) * peak_cm / 13'000) : now < 40'000 ? int32_t((40'000 - now) * peak_cm / 15'000) : 0;
    int16_t temperature = static_cast<int16_t>(ambient_centi - altitude * 65 / 10'000);
    uint32_t pressure = static_cast<uint32_t>(base_pressure * std::pow(1 - altitude / 4'433'000.f, 5.255f));
    bool bmp_ok = std::strcmp(fault, "bmp") || now < 18'000 || now >= 19'000;
    cansat::Reading reading{bmp_ok, temperature, pressure};
    cansat::Frame frame = flight.tick(now, reading);
    bool tx_ok = std::strcmp(fault, "tx") || now != 16'000;
    flight.radio_result(tx_ok);
    cansat::Frame on_air = frame;
    if (!std::strcmp(fault, "crc") && now == 16'000) ++on_air.pressure_pa;
    bool dropped = !std::strcmp(fault, "drop") && now >= 16'000 && now < 17'000;
    bool received = tx_ok && !dropped && cansat::valid(on_air);
    std::printf("%lu,%ld,%d,%lu,%u,%u,%u,%d,%lu,%ld,%u,%u,%u,%u,%u\n",
      static_cast<unsigned long>(now), static_cast<long>(altitude), temperature, static_cast<unsigned long>(pressure),
      bmp_ok, frame.seq, frame.state, frame.temp_centi_c, static_cast<unsigned long>(frame.pressure_pa),
      static_cast<long>(frame.relative_altitude_cm), frame.flags, frame.health, frame.event, tx_ok, received);
  }
}
