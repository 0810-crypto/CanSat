# CanSat primary mission

This folder contains the **primary mission only**. The flight Pico 2 reads BMP280 pressure and temperature, calculates altitude and mission state, then transmits telemetry with an RFM69HCW radio. There is no LiDAR build or LiDAR wiring in this version.

**Want to run it?** Start with [RUN_COMMANDS.md](RUN_COMMANDS.md). It gives the exact Terminal commands for a CanSat folder in Downloads, including simulation, firmware build, flashing, and live USB output.

**For the other coder:** [CODE_WALKTHROUGH.md](CODE_WALKTHROUGH.md) follows the readings from sensor to radio to dashboard and explains each source file with line links, units, state changes, tests, and the places to edit.

## Find the right file

| What you need | File |
| --- | --- |
| BMP280 and radio drivers, flight loop, ground receiver loop | `pico/firmware.cpp` |
| Sensor and radio GPIO pins, BMP280 address | `pico/pins.hpp` |
| Pressure reading, altitude, mission state, packet creation | `pico/flight.hpp` |
| 23-byte telemetry packet, CRC, CSV format | `pico/cansat.hpp` |
| Live laptop terminal display and CSV logging | `ground/dashboard.cpp` |
| Browser viewer for a saved flight log | `ground/dashboard.html` |

`simulation/` runs laptop tests and the circuit view. `tests/` contains checks. `build/` is generated output, including the `.uf2` files to flash. `Makefile` and `CMakeLists.txt` are build instructions.

## Wire the flight Pico 2

| Part | Pico 2 connection |
| --- | --- |
| BMP280 | SDA GP0, SCL GP1, 3V3, GND |
| RFM69HCW | MISO GP16, NSS/CS GP17, SCK GP18, MOSI GP19, RESET GP20, 3V3, GND, antenna |

The BMP280 address in `pico/pins.hpp` is the preferred default. Flight startup checks for a BMP280 at both `0x76` and `0x77` and reports the IDs over USB. Check the actual radio module's voltage and pin labels before applying power. The provided ground receiver firmware expects another Pico with an RFM69HCW wired to the same radio pins. A laptop radio stick has not yet been identified or checked for compatibility.

## Check the code on your Mac

In Terminal:

```sh
cd ~/Downloads/CanSat
make check
make demo
```

`make check` runs compiled C++ flight logic through a normal launch and BMP, CRC, packet loss, and radio timeout faults. A failure exits with an error. `make demo` writes sample logs under `simulation/logs/`. Open `ground/dashboard.html` and choose a log to see the flight timeline.

For the circuit view, run `make circuit` and open the local address printed in Terminal. It shows the BMP280, flight Pico, radio link, and ground receiver. You can load your C++ files, change launch conditions, inject faults, and compile the Pico firmware from the page. This is a software model using the shared flight logic. It cannot verify physical wiring, power, sensor electronics, or RF performance.

## Build and flash

```sh
cd ~/Downloads/CanSat
export PICO_SDK_PATH="$HOME/pico-sdk"
make firmware
```

If the ARM compiler is in a custom folder rather than your PATH, also set `PICO_TOOLCHAIN_PATH` to that folder before building. The Pico 2 flight image is `build/pico2-arm/cansat_flight.uf2`. Hold **BOOTSEL** while connecting the flight Pico, then copy that file to the mounted `RP2350` drive. The ground Pico image, if you use the provided two-Pico receiver, is `build/pico2-arm/cansat_ground.uf2`. The flight image mirrors telemetry and startup device IDs over USB so you can check the connected sensor without a ground receiver.

After a ground Pico is connected by USB, `make host` builds the laptop display. Then pipe its serial CSV into the dashboard, replacing the example device name with the one on your Mac:

```sh
cat /dev/cu.usbmodemYOUR_GROUND_PICO | build/dashboard
```

The dashboard saves raw logs under `logs/`. Both Pico images must be built from this primary-only version: the packet is now 23 bytes, protocol version 3. `flags & 1` means a valid BMP280 sample; `flags & 2` means the previous radio transmit timed out. Test with real hardware on the bench before flight.
