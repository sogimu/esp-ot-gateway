# ESP OpenTherm Gateway

[![Tests](https://github.com/sogimu/esp-ot-gateway/actions/workflows/tests.yml/badge.svg)](https://github.com/sogimu/esp-ot-gateway/actions/workflows/tests.yml)
[![Coverage](https://img.shields.io/badge/coverage-report-blue)](https://sogimu.github.io/esp-ot-gateway/coverage/)

WiFi-connected OpenTherm boiler controller for ESP32. Implements a full OpenTherm v2.2 master stack, PID room-temperature control with external DS18B20 sensors, gas consumption estimation, DHW session prediction, and a responsive web dashboard for monitoring and control of the **Baxi Duo-tec Compact** gas boiler.

---

## Features

- **Full OpenTherm v2.2 protocol** — Manchester encoding/decoding via GPIO ISR + 500 µs hardware timer
- **Embedded web dashboard** — Dark-theme single-page app served directly from ESP32 on port 80
- **REST API** — JSON endpoints for boiler state, control commands, event log, statistics, and heating schedule
- **PID room-temperature control** — Closed-loop CH regulation using external DS18B20 temperature sensors (T1/T2), configurable Kp/Ki/Kd/dt, cycle lockout protection, and hysteresis
- **24-hour heating schedule** — Hourly CH setpoints driven by NTP time sync with configurable timezone (UTC−12 to +14)
- **Three CH control modes** — Manual setpoint, PID auto-regulation, and time-based schedule, selectable from the web UI
- **DHW priority logic** — Automatic 3-way valve control with configurable hysteresis for indirect hot water tanks
- **DHW via CH2 enable** — On Baxi boilers, DHW mode requires the `CH2_ENABLE` flag to switch the 3-way valve, even though `SlaveConfig` reports CH2=0
- **DHW session prediction** — Kalman2D filter estimates remaining heating time and uncertainty for the current DHW cycle, displayed on the web dashboard
- **Gas flow estimation** — Estimates instant gas flow (m³/h) from burner modulation and return temperature, with multi-window EMA averaging (1h / 3h / 12h / 24h / 7d) and cumulative integral tracking
- **Fault monitoring** — ASF flags, OEM diagnostic codes, one-shot fault reset
- **Event log** — 512-entry ring buffer with 5 categories (System, User, Equipment, Mode, Boot) and real-time filtering in the web UI
- **Crash diagnostics** — Reset reason detection on every boot, core dump saved to flash on panic, backtrace decoded offline via `decode_crash.sh`
- **Modulation statistics** — 1000-bin histogram (0.1% resolution), percentile analysis (p1–p99), burn cycle tracking (256-entry ring), median/avg burn & pause times, burner runtime hours — on the Statistics tab and `/api/stats`
- **Relay control** — GPIO 23 relay closes at boot (normal operation) and opens on power loss (fail-safe)

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32 (240 MHz, 520 KB RAM, 2 MB flash, built-in WiFi 802.11 b/g/n) |
| Protocol | OpenTherm v2.2 (master role) |
| OT TX pin | GPIO 4 → SmartTherm adapter → boiler bus |
| OT RX pin | GPIO 16 ← SmartTherm adapter ← boiler bus |
| Relay pin | GPIO 23 → SmartTherm relay (HF33F-005-ZS3, NO contact) |
| Sensor T1 | GPIO 15 — DS18B20 temperature sensor |
| Sensor T2 | GPIO 26 — DS18B20 temperature sensor |

### Hardware Notes

| Note | Detail |
|------|--------|
| **Relay behavior** | The relay closes (energised) at boot and opens (de-energised) on power loss — no extra code required. |
| **SmartTherm adapter** | TX is active-low: GPIO LOW = bus active, GPIO HIGH = bus idle. RX is active-high. |
| **DS18B20 sensors** | Two 1-Wire temperature sensors on GPIO 15 and 26, using internal pull-up resistors (bit-banged protocol). Polling cycle ~1.6 seconds. |
| **Baxi-specific** | The boiler rejects DHW setpoint writes (ID=56) and uses its own internal setpoint (~60°C). The `CH2_ENABLE` flag (bit 4) is required to switch the 3-way valve to DHW despite `SlaveConfig` reporting CH2=0. |

### Tested Hardware

| Component | Detail |
|-----------|--------|
| Development board | [ESP32 development board](https://ozon.ru/t/91Jlr6E) — [user guide](docs/userguideSmartOT_01e.pdf) |
| Boiler | [Baxi Duo-tec Compact 1.24](https://shop.baxi.ru/products/duo-tec-compact-1-24) |

---

## Quick Start (Ubuntu)

### 1. Set WiFi credentials

Edit `main/infrastructure/driving/wifi_config.h`:

```
WIFI_SSID  "your_network"
WIFI_PASS  "your_password"
```

### 2. Install ESP-IDF

```bash
bash scripts/setup.sh
```

This installs:
- System packages (`git`, `cmake`, `python3`, `ninja`, ...)
- **ESP-IDF v5.2.2** into `~/esp/esp-idf`
- Xtensa toolchain (`xtensa-esp32-elf-gcc`)

### 3. Activate the ESP-IDF environment

```bash
source ~/esp/esp-idf/export.sh
```

> Add this line to `~/.bashrc` to avoid running it on every session.

### 4. Build

```bash
bash scripts/build.sh
# or directly:
idf.py build
```

### 5. Flash

```bash
bash scripts/flash.sh /dev/ttyUSB0
# or:
idf.py -p /dev/ttyUSB0 flash
```

### 6. Open the web dashboard

Navigate to `http://<device-ip>` (the IP is assigned by your router via DHCP).

---

## Web Dashboard

The dashboard auto-refreshes and features 5 tabbed views:

### Status tab

| Card / Element | Description |
|----------------|-------------|
| CH supply temp | °C |
| Return temp | °C |
| DHW tank temp | °C |
| Modulation | Burner modulation % |
| T1 / T2 | External DS18B20 room sensor readings |
| 3-way valve | CH / DHW / stopped |
| Boiler SVG | Visual diagram with burner flame (on/off), pump (spinning/stopped), valve position, CH & DHW setpoints |
| DHW prediction | Remaining heating time and uncertainty band for current DHW session |

Badges show: boiler connection status, flame on/off, CH active, DHW active, fault status with animated fault indicator.

### Log tab

Real-time event feed with category filter buttons: System, User, Equipment, Mode, Boot, ALL. Colour-coded entries with timestamps. Boot/crash entries appear in red.

### Statistics tab

| Metric | Description |
|--------|-------------|
| Modulation percentiles | p1, p10, p25, p50, p75, p90, p99 (0.1% resolution) |
| Burn cycles | Total recorded cycles |
| Median burn / pause | Median duration of burn and pause phases |
| Average burn / pause | Mean duration of burn and pause phases |
| Burner runtime | Total hours of burner operation |
| Ratio p90/max, p10/p50, p99−p90 | Modulation distribution coefficients |
| Gas instant flow | Estimated gas consumption (m³/h) |
| Gas integral | Cumulative gas volume (m³) |
| Gas averages | 1h / 3h / 12h / 24h / 7d rolling averages |
| Corrections log | Gas meter calibration correction history |

### Control tab

Three CH heating modes selectable:

- **Manual** — Fixed CH setpoint (20–80 °C)
- **PID** — Room-temperature regulation with configurable parameters:
  - Room sensor selection (T1 or T2)
  - Target temperature (16–28 °C)
  - Kp / Ki / Kd coefficients
  - Control interval (30–120 s)
  - Cycle lockout protection (60–600 s)
  - Hysteresis (0.1–3.0 °C)
- **Schedule** — 24-hour grid editor with hourly CH setpoints

DHW section:
- Enable toggle + setpoint (40–80 °C)
- Hysteresis (0.5–10 °C)

Emergency shutdown button stops all heating.

### Info tab

- OEM fault codes and ASF flags
- OEM diagnostic code
- CH/DHW setpoint bounds (min/max)
- Device IP address
- Boiler software version / OpenTherm version
- ESP32 system time
- Timezone offset (UTC−12 to +14)
- NTP server configuration (two servers)
- Runtime counters (burner starts, CH pump starts, DHW valve starts, DHW burner starts)
- Runtime hours (burner, CH pump, DHW valve, DHW burner)

---

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web dashboard |
| GET | `/api/status` | Full boiler state as JSON |
| GET | `/api/log` | Event log (latest 512 entries) as JSON |
| GET | `/api/stats` | Modulation statistics, percentiles, burn cycles, gas data as JSON |
| POST | `/api/control` | Set CH/DHW enable, setpoints, CH mode, PID config, schedule, fault reset, timezone, NTP servers, gas meter, K calibration, DHW hysteresis |
| GET | `/api/schedule` | Read 24-hour heating schedule |
| POST | `/api/schedule` | Update 24-hour heating schedule |

```bash
# Get boiler state
curl http://<device-ip>/api/status

# Control
curl -X POST http://<device-ip>/api/control \
  -H "Content-Type: application/json" \
  -d '{"ch_enable":1,"ch_setpoint":65,"dhw_enable":1,"dhw_setpoint":55}'

# Update schedule (24 values, one per hour)
curl -X POST http://<device-ip>/api/schedule \
  -H "Content-Type: application/json" \
  -d '{"schedule":[20,20,20,20,20,20,22,22,22,22,22,22,22,22,22,22,22,22,22,22,20,20,20,20]}'
```

---

## OpenTherm Parameters (Baxi Duo-tec Compact)

| ID | Parameter | Access |
|----|-----------|--------|
| 0 | Boiler status flags | R/W |
| 1 | CH setpoint | W |
| 3 | Slave config | R |
| 5 | Fault / ASF flags | R |
| 17 | Burner modulation % | R |
| 18 | System pressure (bar) | R |
| 25 | CH supply temperature | R |
| 26 | DHW tank temperature | R |
| 28 | Return temperature | R |
| 48 | DHW setpoint bounds | R |
| 49 | CH setpoint bounds | R |
| 56 | DHW setpoint | R/W |
| 57 | Max CH setpoint | W |
| 115 | OEM diagnostic code | R |
| 124–126 | Protocol / software versions | R/W |

---

## Tests

```bash
# Install Catch2 (Ubuntu)
sudo apt-get install catch2

# Build and run
bash test/run_tests.sh

# With sanitizers (default: ON)
cmake -B build_test -S test
cmake --build build_test && ./build_test/run_tests

# Coverage report (requires lcov)
cmake -B build_test -S test -DENABLE_SANITIZERS=OFF -DENABLE_COVERAGE=ON
cmake --build build_test --target coverage
# HTML report in build_test/coverage/html/index.html
```

## Project Structure

```
esp-ot-gateway/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── decode_crash.sh
├── .github/workflows/tests.yml       # CI: sanitizers + coverage
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp                      # Composition root (8 phases)
│   ├── domain/                       # Zero dependencies
│   │   ├── value_objects/            # Temperature, CH_Schedule, Modulation, PidConfig, BoilerStatus
│   │   ├── entities/                 # HeatingSystem aggregate
│   │   └── services/                 # PidAlgorithm, Kalman1D, Kalman2D
│   ├── application/                  # Use cases + ports, no ESP-IDF
│   │   ├── ports/
│   │   │   ├── driving/              # IConfigureSystem, IConfigurePid, IGasCalibration, ...
│   │   │   └── driven/               # IHeatingStateStore, IBoilerHardware, ITimeSource, ...
│   │   ├── use_cases/                # BoilerPoll, SensorsPoll, PidPoll, SystemConfig, GasCorrection
│   │   └── services/                 # ModulationStats, BurnCycle, GasFlow, DHWPredict, DHWHysteresis, Schedule
│   ├── infrastructure/
│   │   ├── driven/                   # Adapters: OT, Sensors, NVS, EventLog, SNTP, CrashDiag, ...
│   │   ├── driving/                  # HttpController, MainPollerTask, WifiInit, web_page.h, wifi_config.h
│   │   └── freertos/                 # SharedMutex (read-write lock)
│   └── c_legacy/                     # opentherm.c, sensors.c (unchanged)
├── test/
│   ├── CMakeLists.txt                # Host-only: Catch2, sanitizers, coverage
│   ├── run_tests.sh
│   ├── fakes/                        # Fake adapters for testing
│   └── test_*.cpp                    # 181 tests, 443 assertions
├── docs/
│   └── *.pdf
└── scripts/
    ├── setup.sh
    ├── build.sh
    └── flash.sh
```

---

## Debugging

```bash
# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
# Exit: Ctrl+]

# Check firmware size
idf.py size

# Size breakdown by component
idf.py size-components
```

### Crash Diagnostics

On every boot the firmware logs the reset reason to the event log (visible in the web Journal tab under the "Boot" filter). After a panic (exception), a core dump is saved to flash and recovered on the next boot — the crash log shows the faulting task, program counter, exception cause, and a backtrace.

To decode backtrace addresses to source locations:

```bash
./decode_crash.sh 0x400818ba 0x40089a71 0x400919cd
```

This uses `xtensa-esp32-elf-addr2line` against `build/esp-ot-gateway.elf`.

### Erase NVS (factory reset)

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash
```

> This also erases the core dump partition and all persisted config/statistics.

---

## Notes

- WiFi credentials are stored in plaintext in `main/infrastructure/driving/wifi_config.h` — keep the device on a trusted local network.
- HTTP endpoints have no authentication; the dashboard is intended for LAN use only.
- The OpenTherm initialisation handshake (slave version exchange) runs at startup and repeats every 60 minutes to ensure DHW control stays active on some Baxi firmware versions.
- **Inter-frame gap**: A minimum 100 ms pause between OpenTherm transactions is required by the spec; violating this causes `NO_RESP` errors and boiler resets on Baxi hardware.
- **DHW setpoint (ID=56)**: The Baxi Duo-tec Compact does NOT accept DHW setpoint writes — it returns `NO_RESP` and uses its own internal setpoint (~60°C). Control DHW firing via `DHW_ENABLE` (bit 1) in the Status flags instead, paired with hysteresis in firmware.
- **CH2 enable**: Despite `SlaveConfig` reporting CH2=0, the Baxi requires `CH2_ENABLE` (bit 4) in the Status flags to switch the 3-way valve to the indirect DHW tank. Without CH2_ENABLE the valve stays on CH.
- **Modulation floor**: When the burner is active at minimum output the boiler reports `0x0000` (0% modulation). The firmware converts this to 0.3% for accurate percentile calculations.
- **Relay**: The SmartTherm board relay (GPIO 23, HF33F-005-ZS3) is set HIGH at boot, closing the NO contact. On power loss the coil de-energises and the contact opens automatically — no shutdown code needed.
- **DS18B20 sensors**: Two sensors are polled on a ~1.6 s cycle via software 1-Wire. Internal pull-up resistors (~45 kΩ) are used; readings are CRC-8 validated with automatic retry on failure. The polling task runs at FreeRTOS priority 4.
- **Crash recovery**: Core dumps are saved to a dedicated 704 KB flash partition. On the next boot the crash details (task name, PC, exception cause, backtrace) are logged to the event journal. Use `decode_crash.sh` to resolve addresses to source code locations.
- **Build**: Always source the ESP-IDF environment first: `source ~/esp/esp-idf/export.sh && idf.py build`.
