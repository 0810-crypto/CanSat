# CanSat code walkthrough

This is the companion to [README.md](README.md). It explains the **current primary mission** so another coder can follow the data, change it safely, and see what is tested. The source links point to the relevant lines. Some source files deliberately pack several statements onto one line; each operation on those lines is unpacked below.

## Read this first: the whole path

~~~text
BMP280 pressure + temperature
    → flight Pico reads the sensor over I²C
    → Flight::tick calculates relative altitude and mission state
    → Frame gets a sequence number and CRC
    → RFM69HCW sends the frame over radio
    → ground Pico validates the frame and prints CSV over USB
    → laptop dashboard displays and saves the CSV
~~~

The source for the **flight Pico** is [pico/firmware.cpp](pico/firmware.cpp). The same file makes the **ground Pico** image when CMake defines GROUND_STATION. [pico/flight.hpp](pico/flight.hpp) contains the mission logic shared by the real flight build and the laptop simulators. The laptop [ground/dashboard.cpp](ground/dashboard.cpp) reads USB serial text; it does not drive the radio itself.

The circuit simulator and the saved-log viewer are separate laptop tools. They do not run on the Pico. There is currently no LiDAR mission, onboard file logging, flight command receiver, or confirmed driver for the unidentified laptop radio stick.

### Quick C++ notation used here

| Notation | Meaning in this project |
| --- | --- |
| <code>cansat::Frame</code> | Frame inside the cansat namespace. The <code>::</code> selects a name from a namespace or class. |
| <code>uint8_t</code>, <code>uint32_t</code>, <code>int32_t</code> | Fixed-size integers: unsigned 8-bit, unsigned 32-bit, signed 32-bit. Fixed sizes matter for the radio packet. |
| <code>flags & BMP_OK</code> | Test whether the BMP_OK bit is set. |
| <code>flags |= BMP_OK</code> | Set that bit without clearing other bits. |
| <code>x ? a : b</code> | Use a when x is true; otherwise use b. |
| <code>static_cast&lt;T&gt;(x)</code> | Explicitly convert x to type T. |
| <code>#ifdef GROUND_STATION</code> | Compile the ground main function only for the ground target; the other branch makes the flight image. |
| <code>100'000</code> | Digit separators in a number; this is one hundred thousand. |

## Units and the packet

| Name | Meaning | Example |
| --- | --- | --- |
| ms | Milliseconds since Pico boot | 12,500 means 12.5 seconds |
| temp_centi_c | Temperature in hundredths of a degree Celsius | 2,145 means 21.45 °C |
| pressure_pa | Atmospheric pressure in pascals | 101,325 Pa |
| relative_altitude_cm | Height relative to the preflight pressure baseline, in centimetres | 1,000 means 10 m |
| seq | Packet counter, wrapping after 255 | 255 is followed by 0 |
| flags bit 0 | This packet has a valid BMP280 reading | value 1 |
| flags bit 1 | The **previous** radio send timed out | value 2 |
| health bit 0 | BMP280 reading failed in this packet | value 1 |
| health bit 1 | Previous radio send failed | value 2 |
| event | State transition that occurred on this tick; zero means none | 3 means ASCENT |

The frame is **23 bytes** and protocol version **3**. Both the flight transmitter and ground receiver must use this version. CSV has nine columns in the order defined by [CSV_HEADER](pico/cansat.hpp#L29). The radio packet has extra binary fields for its tag, version, and CRC; those are checked before CSV output and are not CSV columns.

The binary Frame layout is useful if the laptop radio receiver later turns out to expose raw packet bytes. The Pico 2 stores the multi-byte integers here in little-endian order. The RFM69 FIFO transmission puts a one-byte length value (23) before these 23 Frame bytes.

| Frame byte offset | Field | Bytes |
| --- | --- | --- |
| 0 | tag, always 0xCA | 1 |
| 1 | protocol version, now 3 | 1 |
| 2 | seq | 1 |
| 3 | state | 1 |
| 4–7 | ms | 4 |
| 8–9 | temp_centi_c, signed | 2 |
| 10–13 | pressure_pa | 4 |
| 14–17 | relative_altitude_cm, signed | 4 |
| 18 | flags | 1 |
| 19 | health | 1 |
| 20 | event | 1 |
| 21–22 | CRC over bytes 0–20 | 2 |

The CRC starts at zero and uses polynomial 0x1021, with no final XOR. A generic radio stick still needs compatible modulation, frequency, packet framing, and a way to expose those bytes; the layout alone does not establish that it will work.

## Pico source, line by line

### [pico/pins.hpp](pico/pins.hpp)

| Lines | What they do |
| --- | --- |
| [1–3](pico/pins.hpp#L1-L3) | Include guard and namespace. A namespace keeps pin names separate from other C++ names. |
| [4](pico/pins.hpp#L4) | Put BMP280 SDA on GP0 and SCL on GP1. These are I²C signals, not power pins. |
| [5](pico/pins.hpp#L5) | Map the radio SPI pins: MISO GP16, chip select GP17, clock GP18, MOSI GP19, reset GP20. |
| [6](pico/pins.hpp#L6) | Set the BMP280 I²C address to 0x76. Some boards use 0x77; confirm the actual module before changing it. |

Change GPIO numbers here, then rebuild firmware. The simulation circuit diagram also reads this file. Recheck the physical wiring before powering the boards.

### [pico/cansat.hpp](pico/cansat.hpp): packet, CSV, altitude, and states

| Lines | What they do |
| --- | --- |
| [1–6](pico/cansat.hpp#L1-L6) | Include integer, printing, and math support, then open the cansat namespace. |
| [7](pico/cansat.hpp#L7) | Set the wire protocol version. The receiver rejects other versions. |
| [8–20](pico/cansat.hpp#L8-L20) | Pack Frame without compiler padding and require exactly 23 bytes. Fields are tag 0xCA, version, sequence, state, boot time, temperature, pressure, relative altitude, flags, health, event, and CRC. |
| [22–26](pico/cansat.hpp#L22-L26) | Calculate CRC-16/XMODEM over bytes: start at zero; XOR each byte into the high CRC byte; shift eight times and apply polynomial 0x1021 when the top bit is set. This detects accidental packet changes. |
| [27](pico/cansat.hpp#L27) | Write the calculated CRC into the last two bytes of a frame before transmission. |
| [28](pico/cansat.hpp#L28) | Accept a received frame only if its tag, version, and recomputed CRC all match. |
| [29–32](pico/cansat.hpp#L29-L32) | Define the nine-column CSV header and print one frame as one CSV line. Integer units stay unchanged; the dashboard converts them for display. |
| [34–35](pico/cansat.hpp#L34-L35) | Define the two current flag bits and two health bits. The bit values can be combined with bitwise OR. |
| [36–37](pico/cansat.hpp#L36-L37) | Define state IDs and event IDs. Their order is part of the packet meaning; if changed, update both dashboards and tests. |
| [38–41](pico/cansat.hpp#L38-L41) | Convert pressure to altitude relative to the launch baseline. Zero or missing pressure yields zero. The formula is an atmospheric approximation; it is not a direct height sensor. |
| [42–46](pico/cansat.hpp#L42-L46) | Accumulate pressure samples during preflight, then return their integer average as the launch baseline. |
| [47–50](pico/cansat.hpp#L47-L50) | Store the current mission state, stability timer, landing timer, and highest measured altitude. |
| [51–53](pico/cansat.hpp#L51-L53) | On the first update, leave BOOT, enter PREFLIGHT, start its timer, and report BOOTED. |
| [54](pico/cansat.hpp#L54) | In PREFLIGHT, a missing BMP reading restarts the stability timer. After five seconds with valid readings, enter READY. |
| [55](pico/cansat.hpp#L55) | Without a valid BMP reading, do not advance the remaining flight-state transitions. |
| [56](pico/cansat.hpp#L56) | From READY, enter ASCENT once relative altitude reaches 1,000 cm (10 m). Record that height as the first peak. |
| [57](pico/cansat.hpp#L57) | During ASCENT, update the peak. Enter DESCENT once height falls at least 300 cm (3 m) below it. |
| [58–59](pico/cansat.hpp#L58-L59) | During DESCENT, require altitude at or below 300 cm (3 m) for three seconds before declaring LANDED. If it rises above 3 m, restart that timer. This is a coarse barometric rule. |
| [60–63](pico/cansat.hpp#L60-L63) | Enter RECOVERY when 30 seconds have passed since the landing timer started, then otherwise return no new event. This is about 27 seconds after LANDED if the 3-second landing window was uninterrupted. |

State values are 0 BOOT, 1 PREFLIGHT, 2 READY, 3 ASCENT, 4 DESCENT, 5 LANDED, 6 RECOVERY. Event values are 0 NONE, 1 BOOTED, 2 READY, 3 ASCENT, 4 DESCENT, 5 LANDED, 6 RECOVERY, 7 COMMAND. COMMAND is reserved in the enum; this firmware does not receive ground commands.

### [pico/flight.hpp](pico/flight.hpp): one telemetry tick

| Lines | What they do |
| --- | --- |
| [1–7](pico/flight.hpp#L1-L7) | Include shared definitions. Reading holds the BMP success flag, temperature, and pressure supplied by either the real sensor driver or a simulator. |
| [9–15](pico/flight.hpp#L9-L15) | Keep one reusable Frame, pressure Calibrator, Mission state machine, sequence counter, launch baseline, and previous radio failure flag. |
| [17](pico/flight.hpp#L17) | Set the constant tag and protocol version once. |
| [18–23](pico/flight.hpp#L18-L23) | Start a tick: put in boot time and the next sequence number; copy the **previous** radio result into flags/health; clear the one-tick event field. |
| [24–29](pico/flight.hpp#L24-L29) | If BMP reading succeeded and pressure is nonzero, copy temperature and pressure. While in PREFLIGHT, add pressure to the baseline average. Calculate relative altitude if a baseline exists. Set BMP_OK. |
| [30–35](pico/flight.hpp#L30-L35) | If BMP failed, set temperature, pressure, and altitude to zero and mark HEALTH_BMP. This prevents an old reading from appearing current. |
| [36–37](pico/flight.hpp#L36-L37) | Advance the mission state machine, then put any event and the resulting state in the frame. |
| [38](pico/flight.hpp#L38) | On the first READY tick, freeze the average preflight pressure as the launch baseline. Subsequent ticks use it for altitude. |
| [39–40](pico/flight.hpp#L39-L40) | Seal the packet with CRC and return it to the radio driver. |
| [42](pico/flight.hpp#L42) | Save whether the last send succeeded. This appears on the **next** tick because sending happens after the current frame was built. |

For example, a successful Reading with temperature 2145 and pressure 100123 is copied into Frame. If the preflight baseline is 101325 Pa, relative_altitude_cm is calculated from those two pressures. The state machine may also set an event, then CRC is added. The radio sends the 23 bytes.

### [pico/firmware.cpp](pico/firmware.cpp): hardware and the two Pico builds

| Lines | What they do |
| --- | --- |
| [1–11](pico/firmware.cpp#L1-L11) | Include flight logic, pin plan, Pico I²C/SPI libraries, and memory copying. Give short local names to the BMP address and radio control pins. |
| [14](pico/firmware.cpp#L14) | rw writes one RFM69 register. The high address bit marks an SPI register write; chip select goes low for the transfer, then high. |
| [15](pico/firmware.cpp#L15) | rr reads one RFM69 register using SPI. The high address bit is clear for a read. |
| [16–17](pico/firmware.cpp#L16-L17) | rf_write and rf_read transfer a block of radio FIFO bytes. These handle bytes, not Frame validation. |
| [18](pico/firmware.cpp#L18) | rmode writes the radio operating-mode register. |
| [20–22](pico/firmware.cpp#L20-L22) | Start SPI at 1 MHz, assign SPI GPIO functions, configure manual chip select and reset, then pulse reset. |
| [23–26](pico/firmware.cpp#L23-L26) | Program RFM69 registers for 9.6 kbps FSK, 5 kHz deviation, nominal 433.92 MHz, packet framing, and payload length. These settings must match the receiving radio and the actual module. |
| [28–31](pico/firmware.cpp#L28-L31) | Prefix the 23-byte Frame with its length, write FIFO, transmit, poll packet-sent for up to 100 ms, return to standby, and report send success/failure. |
| [32–36](pico/firmware.cpp#L32-L36) | If a radio payload is ready, read its length. Read a Frame only when length matches; otherwise drain it. Return true only if the frame tag, version, and CRC pass. |
| [39–40](pico/firmware.cpp#L39-L40) | BMP280 object stores factory calibration coefficients and the intermediate fine temperature value. |
| [41](pico/firmware.cpp#L41) | read selects a BMP280 register over I²C and receives the requested bytes. Both transfers must succeed. |
| [42–43](pico/firmware.cpp#L42-L43) | begin starts I²C at 400 kHz, assigns SDA/SCL, and enables pull-ups. |
| [44](pico/firmware.cpp#L44) | Check the BMP280 chip ID is 0x58, then read 24 bytes of calibration data. A wrong address, disconnected sensor, or wrong chip ID fails initialization. |
| [45](pico/firmware.cpp#L45) | Decode little-endian calibration values. Temperature uses t1–t3; pressure uses p1–p9. |
| [46](pico/firmware.cpp#L46) | Write BMP280 control register 0xF4 with 0x27 to start regular measurements. |
| [48–49](pico/firmware.cpp#L48-L49) | sample reads six measurement bytes at 0xF7 and assembles raw pressure and temperature ADC values. |
| [50](pico/firmware.cpp#L50) | Apply the BMP280 temperature compensation coefficients; output hundredths of a degree and save fine for pressure compensation. |
| [51–52](pico/firmware.cpp#L51-L52) | Apply the BMP280 pressure compensation coefficients using 64-bit intermediate math. Return false if the denominator is zero; output pressure in pascals. |
| [56–61](pico/firmware.cpp#L56-L61) | Ground build: initialize USB serial and radio, enter receive mode, print CSV header, then forever print only accepted frames as CSV. |
| [62–65](pico/firmware.cpp#L62-L65) | Flight build: initialize USB, BMP280, radio, Flight logic, and the scheduling variable. BMP280 begin is attempted once at startup. |
| [66–69](pico/firmware.cpp#L66-L69) | Loop forever. Every roughly 100 ms, create a Reading and sample the BMP280 if startup initialization succeeded. A failed sample can be tried again on the next tick. |
| [70–71](pico/firmware.cpp#L70-L71) | Run one flight tick, send its Frame by radio, and store the result for reporting in the next Frame. |

The flight Pico currently **transmits** telemetry. The included ground Pico currently **receives** it. The ground build is selected by the GROUND_STATION compile definition; it is not a second mission. If only the flight Pico exists, the laptop dashboard has no proven direct radio source until the laptop receiver's hardware and output format are identified.

## Laptop ground files

### [ground/dashboard.cpp](ground/dashboard.cpp): live terminal display and log

| Lines | What they do |
| --- | --- |
| [1–8](ground/dashboard.cpp#L1-L8) | Import standard C++ utilities and define the nine parsed telemetry fields. |
| [10–13](ground/dashboard.cpp#L10-L13) | Read one CSV row with nine values. The extra-character check rejects an old row with additional fields instead of silently assigning wrong columns. |
| [14–16](ground/dashboard.cpp#L14-L16) | If no output filename is given, make one from the current time under logs/. |
| [17–24](ground/dashboard.cpp#L17-L24) | Clear and redraw the terminal with link counts, state/event, temperature, pressure, relative altitude, BMP validity, and previous transmit status. Divide centi-units by 100 for display. |
| [25–28](ground/dashboard.cpp#L25-L28) | Choose an explicit or timestamped log path, create its parent directory, open it for writing, and write the CSV header. Supplying an existing explicit path overwrites that file. |
| [29–30](ground/dashboard.cpp#L29-L30) | Read standard input one row at a time; ignore invalid rows. Calculate missed sequence numbers modulo 256, append valid rows to the log, and redraw the display. |
| [31–32](ground/dashboard.cpp#L31-L32) | Close the log when input ends. |

Run this after the ground Pico appears as a USB serial device. The ground Pico prints CSV; the laptop process reads that text and saves it. The dashboard does not decode raw radio packets itself.

### [ground/dashboard.html](ground/dashboard.html): saved-log viewer

This file is compressed into 11 physical lines. Its logic is still small:

| Lines | What they do |
| --- | --- |
| [1–6](ground/dashboard.html#L1-L6) | Set up the page, styling, file picker, status badge, altitude canvas, four metrics, and event list. |
| [7](ground/dashboard.html#L7) | Define DOM lookup and the state/event labels in the same order as the C++ enums. |
| [8](ground/dashboard.html#L8) | Split a chosen CSV into lines, use its header for column names, convert cells to numbers, and keep rows with numeric time. |
| [9](ground/dashboard.html#L9) | Read the latest row; count missed sequences, invalid BMP samples, and transmit failures; render the metrics/events; draw the altitude line. It does **not** make a live USB connection. |
| [10–11](ground/dashboard.html#L10-L11) | Accept drag-and-drop or file-picker input, read the local CSV in the browser, then call load and render. |

## Build files

### [CMakeLists.txt](CMakeLists.txt)

| Lines | What they do |
| --- | --- |
| [1–5](CMakeLists.txt#L1-L5) | Require the Pico SDK path and import its CMake setup. |
| [6–8](CMakeLists.txt#L6-L8) | Name the project, require C++17, and initialize the SDK. |
| [10–17](CMakeLists.txt#L10-L17) | Define a helper that makes a Pico executable from firmware.cpp, includes pico/, links I²C/SPI, enables USB serial, disables UART serial, and emits UF2 output. |
| [19–21](CMakeLists.txt#L19-L21) | Build cansat_flight and cansat_ground from the same source. Define GROUND_STATION only for the ground target, selecting the other main function. |

### [Makefile](Makefile)

| Lines | What they do |
| --- | --- |
| [1–3](Makefile#L1-L3) | Choose the host C++ compiler and flags; default firmware board to pico2. |
| [5–13](Makefile#L5-L13) | Build the laptop dashboard and terminal simulator; host builds the dashboard. |
| [15–22](Makefile#L15-L22) | demo generates normal and four fault CSV examples, then feeds the normal one through the dashboard logger. |
| [24–33](Makefile#L24-L33) | check compiles/runs unit assertions, dashboard CSV check, simulator fault cases, and the circuit integration check. Any failing command stops make. |
| [35–36](Makefile#L35-L36) | circuit starts the localhost Python circuit server and prints its URL. |
| [38–40](Makefile#L38-L40) | firmware configures the Pico SDK/ARM toolchain and builds both Pico UF2 images. |

### [.gitignore](.gitignore)

The ignore rules keep generated build files, simulated or real logs, Python caches, and macOS metadata out of Git. They do not delete local files. The source, documentation, and checks are the files to commit.

## Simulation and checks, file by file

### [simulation/simulate.cpp](simulation/simulate.cpp): quick terminal launch

| Lines | What they do |
| --- | --- |
| [7–11](simulation/simulate.cpp#L7-L11) | Accept either no fault or one of bmp, crc, drop, tx. Invalid arguments print usage and exit. |
| [12–15](simulation/simulate.cpp#L12-L15) | Create the real Flight class, expected six state-change event IDs, counters, and CSV header. |
| [16–20](simulation/simulate.cpp#L16-L20) | Generate 751 ticks over 75 seconds, at 100 ms spacing. Climb to 100 m, descend, derive synthetic pressure and temperature, and call Flight::tick. |
| [21–30](simulation/simulate.cpp#L21-L30) | Check CRC, event order, altitude accuracy, BMP failure behavior, and radio timeout reporting. |
| [31–37](simulation/simulate.cpp#L31-L37) | Inject a transmit failure, dropped interval, or one byte change after CRC; only accepted frames are printed as CSV. |
| [39–45](simulation/simulate.cpp#L39-L45) | Check the final counts and print PASS or FAIL to stderr. A false result exits nonzero. |

### [simulation/circuit_harness.cpp](simulation/circuit_harness.cpp): trace for the visual circuit page

| Lines | What they do |
| --- | --- |
| [7–14](simulation/circuit_harness.cpp#L7-L14) | Receive peak altitude, ambient temperature, base pressure, and fault from the Python server; check a known CRC reference; create Flight; print trace column names. |
| [15–21](simulation/circuit_harness.cpp#L15-L21) | Generate 751 physical-model steps, calculate synthetic temperature/pressure, and run the same Flight::tick used by firmware. |
| [22–27](simulation/circuit_harness.cpp#L22-L27) | Model transmit failure, CRC corruption, packet drop, and ground acceptance. |
| [28–33](simulation/circuit_harness.cpp#L28-L33) | Print both modeled sensor inputs and actual Frame outputs so the server can compare them. |

### [simulation/circuit_server.py](simulation/circuit_server.py): local API and independent checks

| Lines | What they do |
| --- | --- |
| [1–26](simulation/circuit_server.py#L1-L26) | Import tools, find the project root, choose a local port, create a per-run page token, allowlist uploadable source files, and list expected pin names. |
| [29–41](simulation/circuit_server.py#L29-L41) | Copy workspace files into a temporary directory, replacing only files the user selected in the page. Uploaded originals remain untouched. |
| [44–54](simulation/circuit_server.py#L44-L54) | Read decimal GPIO assignments from pins.hpp and reject missing, repeated, or out-of-range pins. |
| [57–64](simulation/circuit_server.py#L57-L64) | Run a build/simulator process with a timeout and turn a nonzero exit into an error shown in the browser. |
| [67–81](simulation/circuit_server.py#L67-L81) | Validate launch settings, compile the C++ circuit harness against the selected flight headers, run it, parse CSV, and require 751 steps. |
| [82–105](simulation/circuit_server.py#L82-L105) | Independently check mission events, pressure, temperature, altitude, BMP failure, radio timeout, and packet loss. Problems become visible issue messages; they are not silently hidden. |
| [106–107](simulation/circuit_server.py#L106-L107) | Return trace rows, issues, pin plan, board name, fault name, and supplied-file list to the page. |
| [110–122](simulation/circuit_server.py#L110-L122) | For the Compile button, verify Pico SDK/toolchain paths, run CMake, compile flight and ground targets, and return the result. This is a compile check, not a physical circuit check. |
| [125–148](simulation/circuit_server.py#L125-L148) | Serve JSON responses and the circuit HTML page on localhost. The page receives a token tied to this server run. |
| [150–177](simulation/circuit_server.py#L150-L177) | Accept only local, token-bearing /api/run or /api/compile POSTs of bounded size. Build in a temporary directory and return JSON errors when a check fails. |
| [180–184](simulation/circuit_server.py#L180-L184) | Bind to 127.0.0.1, print the URL, and start serving. Restarting changes the token, so refresh old browser tabs. |

### [simulation/circuit.html](simulation/circuit.html): interactive front end

| Lines | What they do |
| --- | --- |
| [1–13](simulation/circuit.html#L1-L13) | Set up page title, CSS, and a note that the displayed pressure/radio conditions are modeled. |
| [14–61](simulation/circuit.html#L14-L61) | Draw BMP280, flight Pico, flight radio, ground radio, and ground Pico as SVG. The colored paths and dots represent model state, not measured voltages or live RF. |
| [63–75](simulation/circuit.html#L63-L75) | Show current time, physical height, packet count, fault count, altitude graph, and timeline controls. |
| [77–100](simulation/circuit.html#L77-L100) | Let the user choose project C++ files, model settings, faults, Run, and Compile. |
| [102–108](simulation/circuit.html#L102-L108) | Store the server token, map chosen filenames to allowed source paths, load selected text files, and collect the current settings into a request. |
| [109–113](simulation/circuit.html#L109-L113) | POST JSON to the local API, show errors, escape untrusted error text for display, and control playback/signals. |
| [114–119](simulation/circuit.html#L114-L119) | Render one trace step, draw two altitude curves, and update displayed GPIO labels from pins.hpp. |
| [120–123](simulation/circuit.html#L120-L123) | Run simulation, display independent check issues, animate or scrub the trace, and compile Pico firmware on demand. |

### [simulation/circuit_check.py](simulation/circuit_check.py)

| Lines | What they do |
| --- | --- |
| [12–21](simulation/circuit_check.py#L12-L21) | Load the server module, prepare current project files in a temporary directory, and require a clean 751-step run. |
| [23–32](simulation/circuit_check.py#L23-L32) | Deliberately break pressure and then temperature copying in a temporary flight.hpp; require the circuit checks to catch each bug. The real source is not edited. |
| [34–51](simulation/circuit_check.py#L34-L51) | Start a throwaway local server and require stale-token requests to return JSON 409 with a refresh instruction. |

### [tests/check.cpp](tests/check.cpp), [tests/dashboard_check.sh](tests/dashboard_check.sh), and [tests/dashboard_expected.csv](tests/dashboard_expected.csv)

| File and lines | What they check |
| --- | --- |
| [check.cpp 4–11](tests/check.cpp#L4-L11) | Known CRC value, valid sealed frame, and rejection after a byte changes. |
| [check.cpp 13–21](tests/check.cpp#L13-L21) | Launch-relative altitude and averaged pressure baseline. |
| [check.cpp 23–34](tests/check.cpp#L23-L34) | Mission cannot enter ascent before healthy preflight; missing BMP data stops a state transition. |
| [check.cpp 36–43](tests/check.cpp#L36-L43) | Flight emits a valid packet, then reports BMP and prior radio failures. |
| [dashboard_check.sh 1–8](tests/dashboard_check.sh#L1-L8) | Build the laptop dashboard, feed one current nine-column row plus an obsolete longer row, and compare the saved log with the expected file. |
| [dashboard_expected.csv 1–2](tests/dashboard_expected.csv#L1-L2) | Golden CSV header and valid row; the obsolete row must not appear. |

## Change guide for your coder

| If you change… | Also check… |
| --- | --- |
| GPIO or BMP address | pico/pins.hpp, physical wiring, circuit pin validation, firmware build |
| Sampling interval | pico/firmware.cpp line 67; timing-dependent mission and simulation expectations |
| Mission thresholds or state order | pico/cansat.hpp Mission::update, both dashboard state labels, simulator expected events, tests |
| Frame fields, size, version, or bit meanings | pico/cansat.hpp, flight and ground firmware together, CSV writer, laptop dashboard, browser viewer, simulators, tests |
| Radio frequency or modem registers | pico/firmware.cpp lines 23–26 on **both** radios; module band and antenna must agree |
| BMP280 compensation | pico/firmware.cpp lines 40–52; test against known sensor data and then a real module |
| Circuit page behavior | simulation/circuit.html and simulation/circuit_server.py; run make check |

Run <code>make check</code> after source changes. Run <code>make firmware</code> to make the Pico UF2 images. Passing either proves only the code paths they exercise; test the actual connected BMP280 and radio link on the bench before flight.
