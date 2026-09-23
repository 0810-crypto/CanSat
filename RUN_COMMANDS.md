# Commands to run CanSat (macOS, Pico 2)

This is the short command sheet for a teammate who has the **CanSat folder in Downloads**. Open Terminal and copy one block at a time. There must be a space after <code>cd</code>: use <code>cd ~/Downloads/CanSat</code>, not <code>cd~/Downloads/CanSat</code> or <code>cd /Downloads/CanSat</code>.

The flight Pico starts its flashed program automatically when powered normally. Terminal commands build it, flash it, or **watch** its USB output; <code>cat</code> does not start the Pico.

## 1. Enter the folder and check the laptop code

~~~sh
cd ~/Downloads/CanSat
make check
~~~

This compiles and tests the shared C++ flight logic, CSV format, and circuit model on the Mac. It does not read the real BMP280 or radio. If this folder is a Git clone and you want the latest version from GitHub first, run <code>git pull --ff-only</code> here. If you do not have the folder yet, get it with:

~~~sh
cd ~/Downloads
git clone https://github.com/0810-crypto/CanSat.git
cd CanSat
~~~

Do not run <code>git clone</code> on top of an existing CanSat folder.

## 2. First-time build tools on a different Mac

If <code>cmake</code>, <code>python3</code>, <code>make</code>, <code>c++</code>, or <code>arm-none-eabi-g++</code> is missing, set up the Mac first. On macOS, install Apple's Command Line Tools if needed, then use Homebrew for the other programs:

~~~sh
xcode-select --install
brew install cmake arm-none-eabi-gcc python
git clone --recursive https://github.com/raspberrypi/pico-sdk.git "$HOME/pico-sdk"
~~~

Run the SDK clone only once; if <code>~/pico-sdk</code> already exists, use it. If <code>brew</code> is missing, install Homebrew from [brew.sh](https://brew.sh/) first. These tool names are from the [Raspberry Pi SDK instructions](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html) and the [Homebrew ARM compiler formula](https://formulae.brew.sh/formula/arm-none-eabi-gcc).

On the original Isaac Mac, the SDK is at <code>~/pico-sdk</code> and the ARM toolchain is at <code>~/arm-toolchain-local</code>. On another Mac, the Homebrew compiler is normally found through PATH, so leave PICO_TOOLCHAIN_PATH unset unless you installed the toolchain in a custom folder.

## 3. Build the actual Pico 2 firmware

~~~sh
cd ~/Downloads/CanSat
export PICO_SDK_PATH="$HOME/pico-sdk"
make firmware
~~~

If your ARM toolchain is in a custom folder rather than PATH, set its path first:

~~~sh
export PICO_TOOLCHAIN_PATH="$HOME/arm-toolchain-local"
make firmware
~~~

This makes <code>build/pico2-arm/cansat_flight.uf2</code> and <code>build/pico2-arm/cansat_ground.uf2</code>. The GitHub repo contains source, not generated UF2 files, so a fresh clone must build before flashing. The first firmware build may take longer because the Pico SDK builds its tools.

## 4. Flash the flight Pico 2

Disconnect power before fixing or moving soldered connections. Once the hardware is ready, hold **BOOTSEL** while plugging the **flight Pico 2** into the Mac. Release BOOTSEL. Finder should show an <code>RP2350</code> drive. Check it, then copy the flight image:

~~~sh
cd ~/Downloads/CanSat
ls /Volumes/RP2350/INFO_UF2.TXT
cp build/pico2-arm/cansat_flight.uf2 /Volumes/RP2350/
~~~

The drive normally disappears as the Pico reboots. **Do not** copy the ground image to the flight Pico. If <code>/Volumes/RP2350</code> is missing, unplug and reconnect while holding BOOTSEL. If you plug in normally without BOOTSEL, the already-flashed code simply starts; no Terminal run command is needed.

## 5. See real sensor and radio status over the flight Pico's USB

Plug in normally, then find its serial device:

~~~sh
ls /dev/cu.usbmodem*
~~~

Use the name that appears on **your** Mac. The following is an example; replace <code>usbmodem1101</code> if yours differs:

~~~sh
cat /dev/cu.usbmodem1101
~~~

Press **Ctrl+C** to stop watching; the Pico keeps running. The first line begins with <code>#</code> and shows the BMP280 chip IDs seen at addresses 0x76 and 0x77, the selected BMP address, and a raw radio version register. The next line is the CSV header. Each later line gives time, sequence, state, temperature, pressure, altitude, flags, health, and event.

If the serial device appears but no lines arrive after several seconds, press **Ctrl+C**. The USB connection alone does not show that the flight loop ran. Recheck the powered wiring and the flashed image; treat the sensor and radio as unverified until real rows appear.

A BMP chip ID of <code>0x58</code> at either address means the expected BMP280 responded at startup. Valid BMP samples set flags bit 0 (numeric value <code>1</code>). A previous radio transmit timeout sets flags bit 1 (numeric value <code>2</code>). The radio version number is a chip-register read, **not** proof that packets reached a receiver. Zero temperature and pressure plus BMP health bit 0 (numeric value <code>1</code>) mean the sensor reading failed. Check the physical connections before treating that as a software result.

This USB stream mirrors **flight transmissions** for bench checks. It does not prove that a ground receiver heard them.

## 6. Ground receiver and dashboard, if using the provided second Pico

The included ground firmware requires another Pico 2 with an RFM69HCW wired to the radio pins in [README.md](README.md). Hold BOOTSEL while connecting that **ground Pico**, then:

~~~sh
cd ~/Downloads/CanSat
cp build/pico2-arm/cansat_ground.uf2 /Volumes/RP2350/
make host
ls /dev/cu.usbmodem*
cat /dev/cu.usbmodemYOUR_GROUND_PICO | build/dashboard
~~~

Replace <code>YOUR_GROUND_PICO</code> with the actual device suffix. The dashboard shows received packets and saves CSV under <code>logs/</code>. Both Pico images must come from the same protocol version. A generic laptop radio stick is **not yet supported** by this repo; its model and output format have not been identified.

## 7. Run simulations without any Pico connected

~~~sh
cd ~/Downloads/CanSat
make check
make demo
open ground/dashboard.html
~~~

The <code>make demo</code> command creates example CSVs in <code>simulation/logs/</code>; choose one in the browser dashboard. For the interactive circuit model, run:

~~~sh
cd ~/Downloads/CanSat
make circuit
~~~

Open the <code>http://127.0.0.1:...</code> address printed by Terminal. The circuit view compiles the shared C++ flight logic with simulated pressure, temperature, and radio conditions. It is a software check, not a test of solder joints, sensor electronics, or RF reception.
