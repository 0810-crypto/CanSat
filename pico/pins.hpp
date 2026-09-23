#pragma once

namespace pins {
constexpr unsigned BMP_SDA = 0, BMP_SCL = 1;
constexpr unsigned RF_MISO = 16, RF_CS = 17, RF_SCK = 18, RF_MOSI = 19, RF_RST = 20;
constexpr unsigned char BMP_ADDRESS = 0x76; // Change to 0x77 only if your BMP280 scan reports it.
}
