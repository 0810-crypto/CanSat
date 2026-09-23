#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

struct Telemetry { unsigned ms, seq, state, pressure, flags, health, event; int temp, altitude; };

static bool parse(const char *line, Telemetry &t) {
  char extra;
  return std::sscanf(line, "%u,%u,%u,%d,%u,%d,%u,%u,%u %c", &t.ms, &t.seq, &t.state, &t.temp, &t.pressure, &t.altitude, &t.flags, &t.health, &t.event, &extra) == 9;
}
static std::string default_log() {
  std::time_t now = std::time(nullptr); char stamp[32]; std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now)); return "logs/cansat-" + std::string(stamp) + ".csv";
}
static void render(const Telemetry &t, unsigned received, unsigned missed) {
  std::printf("\033[2J\033[HCanSat ground station\n\nLink      %u received, %u missed | latest sequence %u\n", received, missed, t.seq);
  const char *states[] = {"boot", "preflight", "ready", "ascent", "descent", "landed", "recovery"}; const char *events[] = {"", "booted", "ready", "ascent", "descent", "landed", "recovery", "command"};
  std::printf("Mission   %s%s%s\n", t.state < 7 ? states[t.state] : "unknown", t.event ? " | event: " : "", t.event < 8 ? events[t.event] : "unknown");
  std::printf("Atmosphere  %.2f C | %u Pa | relative altitude %.2f m\n", t.temp / 100.f, t.pressure, t.altitude / 100.f);
  std::printf("BMP280      %s | previous radio TX %s\n", (t.flags & 1) ? "valid" : "waiting", (t.flags & 2) ? "timed out" : "ok");
  std::fflush(stdout);
}
int main(int argc, char **argv) {
  std::string path = argc > 1 ? argv[1] : default_log(); std::filesystem::path p(path); if (!p.parent_path().empty()) std::filesystem::create_directories(p.parent_path());
  FILE *log = std::fopen(path.c_str(), "w"); if (!log) return std::perror(path.c_str()), 1;
  std::fputs("ms,seq,state,temp_centi_c,pressure_pa,relative_altitude_cm,flags,health,event\n", log);
  char line[128]; Telemetry t{}; unsigned received = 0, missed = 0, previous = 0; bool has_previous = false;
  while (std::fgets(line, sizeof line, stdin)) if (parse(line, t)) { if (has_previous) missed += (t.seq - previous - 1) & 255; previous = t.seq; has_previous = true; ++received; std::fputs(line, log); std::fflush(log); render(t, received, missed); }
  std::fclose(log);
}
