#include "flight.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

int main(int argc, char **argv) {
  const char *fault = argc == 1 ? "none" : argc == 3 && !std::strcmp(argv[1], "--fault") ? argv[2] : "invalid";
  if (std::strcmp(fault, "none") && std::strcmp(fault, "bmp") && std::strcmp(fault, "crc") && std::strcmp(fault, "drop") && std::strcmp(fault, "tx")) {
    std::fprintf(stderr, "usage: build/simulation/simulate [--fault bmp|crc|drop|tx]\n"); return 2;
  }
  cansat::Flight flight;
  const uint8_t expected[] = {1, 2, 3, 4, 5, 6};
  unsigned event_index = 0, rejected = 0, dropped = 0, bmp_failures = 0, timeout_reports = 0, received = 0;
  std::puts(cansat::CSV_HEADER);
  for (uint32_t now = 0; now <= 75'000; now += 100) {
    int32_t height_cm = now < 12'000 ? 0 : now < 25'000 ? int32_t((now - 12'000) * 10'000 / 13'000) : now < 40'000 ? int32_t((40'000 - now) * 10'000 / 15'000) : 0;
    uint32_t pressure = static_cast<uint32_t>(101325 * std::pow(1 - height_cm / 4'433'000.f, 5.255f));
    bool bmp_ok = std::strcmp(fault, "bmp") || now < 18'000 || now >= 19'000;
    cansat::Frame frame = flight.tick(now, {bmp_ok, static_cast<int16_t>(2000 - height_cm / 1000), pressure});
    if (!cansat::valid(frame)) return std::fprintf(stderr, "FAIL: flight CRC at %u ms\n", now), 1;
    if (frame.event) {
      if (event_index == sizeof expected || frame.event != expected[event_index++]) return std::fprintf(stderr, "FAIL: mission event %u at %u ms\n", frame.event, now), 1;
    }
    if (bmp_ok && std::abs(frame.relative_altitude_cm - height_cm) > 40) return std::fprintf(stderr, "FAIL: altitude at %u ms\n", now), 1;
    if (!bmp_ok) {
      ++bmp_failures;
      if (frame.state != static_cast<uint8_t>(cansat::State::ASCENT) || frame.pressure_pa || !(frame.health & cansat::HEALTH_BMP)) return std::fprintf(stderr, "FAIL: BMP outage handling at %u ms\n", now), 1;
    }
    if (frame.flags & cansat::RADIO_TIMEOUT) ++timeout_reports;
    bool tx_ok = std::strcmp(fault, "tx") || now != 16'000;
    flight.radio_result(tx_ok);
    if (!tx_ok || (!std::strcmp(fault, "drop") && now >= 16'000 && now < 17'000)) { ++dropped; continue; }
    if (!std::strcmp(fault, "crc") && now == 16'000) ++frame.pressure_pa;
    if (!cansat::valid(frame)) { ++rejected; continue; }
    cansat::write_csv(stdout, frame);
    ++received;
  }
  if (event_index != sizeof expected || received != 751 - dropped - rejected ||
      rejected != (std::strcmp(fault, "crc") ? 0u : 1u) ||
      dropped != (!std::strcmp(fault, "drop") ? 10u : !std::strcmp(fault, "tx") ? 1u : 0u) ||
      bmp_failures != (std::strcmp(fault, "bmp") ? 0u : 10u) ||
      timeout_reports != (std::strcmp(fault, "tx") ? 0u : 1u))
    return std::fprintf(stderr, "FAIL: events=%u received=%u dropped=%u rejected=%u bmp=%u timeout=%u\n", event_index, received, dropped, rejected, bmp_failures, timeout_reports), 1;
  std::fprintf(stderr, "PASS: %u events, %u received, %u dropped, %u CRC rejected, %u BMP failures, %u radio timeouts\n", event_index, received, dropped, rejected, bmp_failures, timeout_reports);
}
