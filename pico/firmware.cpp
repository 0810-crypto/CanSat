#include "flight.hpp"
#include "pins.hpp"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include <cstring>

using cansat::Frame;

// Wiring values for all components live in pins.hpp.
constexpr uint8_t BMP = pins::BMP_ADDRESS, RF_CS = pins::RF_CS, RF_RST = pins::RF_RST;

// RFM69HCW radio: used by both the flight and ground Pico builds.
static void rw(uint8_t r, uint8_t v) { uint8_t b[] = {uint8_t(r | 0x80), v}; gpio_put(RF_CS, 0); spi_write_blocking(spi0, b, 2); gpio_put(RF_CS, 1); }
static uint8_t rr(uint8_t r) { uint8_t tx[] = {uint8_t(r & 0x7F), 0}, rx[2]; gpio_put(RF_CS, 0); spi_write_read_blocking(spi0, tx, rx, 2); gpio_put(RF_CS, 1); return rx[1]; }
static void rf_write(const void *p, size_t n) { uint8_t a = 0x80; gpio_put(RF_CS, 0); spi_write_blocking(spi0, &a, 1); spi_write_blocking(spi0, static_cast<const uint8_t *>(p), n); gpio_put(RF_CS, 1); }
static void rf_read(void *p, size_t n) { uint8_t a = 0; gpio_put(RF_CS, 0); spi_write_blocking(spi0, &a, 1); spi_read_blocking(spi0, 0, static_cast<uint8_t *>(p), n); gpio_put(RF_CS, 1); }
static void rmode(uint8_t mode) { rw(0x01, mode); }

static void radio_init() {
  spi_init(spi0, 1'000'000); gpio_set_function(pins::RF_MISO, GPIO_FUNC_SPI); gpio_set_function(pins::RF_SCK, GPIO_FUNC_SPI); gpio_set_function(pins::RF_MOSI, GPIO_FUNC_SPI);
  gpio_init(RF_CS); gpio_set_dir(RF_CS, GPIO_OUT); gpio_put(RF_CS, 1); gpio_init(RF_RST); gpio_set_dir(RF_RST, GPIO_OUT); gpio_put(RF_RST, 1); sleep_ms(1); gpio_put(RF_RST, 0); sleep_ms(10);
  rmode(0x04); rw(0x02, 0x00); rw(0x03, 0x0D); rw(0x04, 0x05); rw(0x05, 0); rw(0x06, 0x52); // 9.6 kbps FSK, 5 kHz deviation
  rw(0x07, 0x6C); rw(0x08, 0x7A); rw(0x09, 0xE1); rw(0x11, 0x8F); // 433.92 MHz, PA0 low-power mode
  rw(0x18, 0x88); rw(0x19, 0x55); rw(0x2C, 0); rw(0x2E, 0x88); rw(0x2F, 0x2D); rw(0x30, 0xC4);
  rw(0x37, 0x90); rw(0x38, 66); rw(0x3C, 0x8F); rw(0x3D, 2); rw(0x6F, 0x30);
}
static bool radio_send(const Frame &f) {
  uint8_t b[1 + sizeof f] = {sizeof f}; std::memcpy(b + 1, &f, sizeof f); rmode(0x04); rf_write(b, sizeof b); rmode(0x0C);
  uint64_t until = time_us_64() + 100'000; while (!(rr(0x28) & 8) && time_us_64() < until) tight_loop_contents(); rmode(0x04); return time_us_64() < until;
}
static bool radio_receive(Frame &f) {
  if (!(rr(0x28) & 4)) return false;
  uint8_t n = rr(0); if (n == sizeof f) rf_read(&f, sizeof f); else while (n--) rr(0); rmode(0x10);
  return n == sizeof f && cansat::valid(f);
}

// BMP280 pressure and temperature sensor: flight Pico only.
struct Bmp280 {
  uint16_t t1, p1; int16_t t2, t3, p2, p3, p4, p5, p6, p7, p8, p9; int32_t fine;
  uint8_t address = BMP, id76 = 0, id77 = 0;
  bool read(uint8_t r, uint8_t *b, size_t n) { return i2c_write_timeout_us(i2c0, address, &r, 1, true, 10'000) == 1 && i2c_read_timeout_us(i2c0, address, b, n, false, 10'000) == int(n); }
  bool begin() {
    i2c_init(i2c0, 400'000); gpio_set_function(pins::BMP_SDA, GPIO_FUNC_I2C); gpio_set_function(pins::BMP_SCL, GPIO_FUNC_I2C); gpio_pull_up(pins::BMP_SDA); gpio_pull_up(pins::BMP_SCL);
    address = 0x76; read(0xD0, &id76, 1);
    address = 0x77; read(0xD0, &id77, 1);
    if (id76 == 0x58) address = 0x76;
    else if (id77 == 0x58) address = 0x77;
    else return false;
    uint8_t c[24]; if (!read(0x88, c, sizeof c)) return false;
    auto u = [&](int i) { return uint16_t(c[i] | uint16_t(c[i + 1]) << 8); }; t1 = u(0); t2 = int16_t(u(2)); t3 = int16_t(u(4)); p1 = u(6); p2 = int16_t(u(8)); p3 = int16_t(u(10)); p4 = int16_t(u(12)); p5 = int16_t(u(14)); p6 = int16_t(u(16)); p7 = int16_t(u(18)); p8 = int16_t(u(20)); p9 = int16_t(u(22));
    uint8_t x[] = {0xF4, 0x27}; return i2c_write_timeout_us(i2c0, address, x, 2, false, 10'000) == 2;
  }
  bool sample(int16_t &tc, uint32_t &pa) {
    uint8_t d[6]; if (!read(0xF7, d, sizeof d)) return false; int32_t ap = (int32_t(d[0]) << 12) | (int32_t(d[1]) << 4) | (d[2] >> 4), at = (int32_t(d[3]) << 12) | (int32_t(d[4]) << 4) | (d[5] >> 4);
    int32_t v1 = ((((at >> 3) - (int32_t(t1) << 1))) * int32_t(t2)) >> 11, v2 = (((((at >> 4) - int32_t(t1)) * ((at >> 4) - int32_t(t1))) >> 12) * int32_t(t3)) >> 14; fine = v1 + v2; tc = int16_t((fine * 5 + 128) >> 8);
    int64_t a = fine - 128000, b = a * a * p6 + ((a * p5) << 17) + (int64_t(p4) << 35); a = ((a * a * p3) >> 8) + ((a * p2) << 12); a = (((int64_t(1) << 47) + a) * p1) >> 33; if (!a) return false;
    int64_t q = (((int64_t(1048576 - ap) << 31) - b) * 3125) / a; a = (int64_t(p9) * (q >> 13) * (q >> 13)) >> 25; b = (int64_t(p8) * q) >> 19; pa = uint32_t(((q + a + b) >> 8) + (int64_t(p7) << 4)) / 256; return true;
  }
};

#ifdef GROUND_STATION
// Ground receiver Pico: receives packets and prints USB telemetry.
int main() {
  stdio_init_all(); radio_init(); rmode(0x10); sleep_ms(1500); std::puts(cansat::CSV_HEADER); Frame f;
  while (true) if (radio_receive(f)) cansat::write_csv(stdout, f);
}
#else
// Flight Pico: BMP280 pressure and temperature, altitude, and radio.
int main() {
  stdio_init_all(); Bmp280 bmp; bool bmp_ok = bmp.begin(); radio_init(); uint8_t radio_version = rr(0x10); cansat::Flight flight; uint32_t next = 0; bool usb_header_sent = false;
  while (true) {
    uint32_t now = to_ms_since_boot(get_absolute_time()); if (int32_t(now - next) < 0) continue; next = now + 100;
    cansat::Reading reading{false, 0, 0};
    if (bmp_ok) reading.ok = bmp.sample(reading.temp_centi_c, reading.pressure_pa);
    Frame f = flight.tick(now, reading);
    flight.radio_result(radio_send(f));
    // Mirror flight telemetry over USB for bench checks; radio still sends the same frame.
    if (stdio_usb_connected()) {
      if (!usb_header_sent) {
        std::printf("# bmp_id_76=0x%02X,bmp_id_77=0x%02X,bmp_address=0x%02X,radio_version=0x%02X\n", bmp.id76, bmp.id77, bmp_ok ? bmp.address : 0, radio_version);
        std::puts(cansat::CSV_HEADER); usb_header_sent = true;
      }
      cansat::write_csv(stdout, f); std::fflush(stdout);
    } else usb_header_sent = false;
  }
}
#endif
