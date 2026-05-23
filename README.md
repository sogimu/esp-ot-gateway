# ESP OpenTherm Gateway

WiFi-connected OpenTherm boiler controller for ESP32. Implements a full OpenTherm v2.2 master stack and serves a responsive web dashboard for remote monitoring and control of the **Baxi Duo-tec Compact** gas boiler.

---

## Features

- **Full OpenTherm v2.2 protocol** — Manchester encoding/decoding via GPIO ISR + 500 µs hardware timer
- **Embedded web dashboard** — Dark-theme single-page app served directly from ESP32 on port 80
- **REST API** — JSON endpoints for boiler state, control commands, event log, statistics, and heating schedule
- **24-hour heating schedule** — Hourly CH setpoints driven by NTP time sync (configurable timezone)
- **DHW priority logic** — Automatic 3-way valve control with hysteresis for indirect hot water tanks (fires when DHW temp drops 2°C below setpoint, stops when setpoint is met)
- **DHW via CH2 enable** — On Baxi boilers, DHW mode requires the `CH2_ENABLE` flag to switch the 3-way valve, even though `SlaveConfig` reports CH2=0
- **Fault monitoring** — ASF flags, OEM diagnostic codes, one-shot fault reset
- **Event log** — 512-entry ring buffer with categories (system, equipment, dhw, schedule), accessible via web dashboard and `/api/log`
- **Modulation statistics** — 1000-bin histogram (0.1% resolution), percentile analysis (p1–p99), burn cycle tracking (256-entry ring), median/avg burn & pause times, burner runtime hours — all available on the Statistics tab and `/api/stats`
- **Relay control** — GPIO 23 relay closes at boot (normal operation) and opens on power loss (fail-safe)

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32 (240 MHz, 520 KB RAM, 4 MB flash, built-in WiFi 802.11 b/g/n) |
| Protocol | OpenTherm v2.2 (master role) |
| TX pin | GPIO 4 → SmartTherm adapter → boiler bus |
| RX pin | GPIO 16 ← SmartTherm adapter ← boiler bus |
| Relay pin | GPIO 23 → SmartTherm relay (HF33F-005-ZS3, NO contact) |

### Hardware Notes

| Note | Detail |
|------|--------|
| **Relay behavior** | The relay closes (energised) at boot and opens (de-energised) on power loss — no extra code required. |
| **SmartTherm adapter** | TX is active-low: GPIO LOW = bus active, GPIO HIGH = bus idle. RX is active-high. |
| **Baxi-specific** | The boiler rejects DHW setpoint writes (ID=56) and uses its own internal setpoint (~60°C). The `CH2_ENABLE` flag (bit 4) is required to switch the 3-way valve to DHW despite `SlaveConfig` reporting CH2=0. |

### Tested Hardware

| Component | Detail |
|-----------|--------|
| Development board | [ESP32 development board](https://ozon.ru/t/91Jlr6E) — [user guide](docs/userguideSmartOT_01e.pdf) |
| Boiler | [Baxi Duo-tec Compact 1.24](https://shop.baxi.ru/products/duo-tec-compact-1-24) |

---

## Quick Start (Ubuntu)

### 1. Set WiFi credentials

```c
// main/wifi_config.h
#define WIFI_SSID  "your_network"
#define WIFI_PASS  "your_password"
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

### 5. Flash and open serial monitor

```bash
bash scripts/flash.sh /dev/ttyUSB0
# or:
idf.py -p /dev/ttyUSB0 flash monitor
```

On success you'll see:

```
I (xxx) main: WiFi connected. IP: <device-ip>
I (xxx) main: Web interface: http://<device-ip>
```

> The IP address is assigned by your WiFi router via DHCP and will vary depending on your network.

### 6. Open the web dashboard

Navigate to the IP address shown in the monitor: `http://<device-ip>`

---


## Web Dashboard

The dashboard auto-refreshes every 2 seconds and features tabbed navigation:

### Status tab

| Parameter | Description |
|-----------|-------------|
| Burner | Active / Off (+ modulation %) |
| CH pump | Running / Stopped |
| DHW pump / 3-way valve | Running / Stopped |
| Fault | None / FAULT |
| CH supply temp | °C |
| Return temp | °C |
| DHW temp | °C |
| Pressure | bar |
| Modulation | % |
| CH / DHW setpoints | °C (shown on boiler SVG diagram) |
| 3-way valve SVG | Visual valve position: ↑CH / ↓DHW / →stopped |

Controls:
- **CH enable** toggle + setpoint slider (20–80 °C)
- **DHW enable** toggle + setpoint slider (35–65 °C)
- **Fault reset** button
- **Timezone offset** selector

### Log tab

Real-time event feed with category filtering (all, system, equipment, dhw, schedule). Colour-coded entries with timestamps.

### Statistics tab

| Metric | Description |
|--------|-------------|
| Modulation percentiles | p1, p10, p25, p50, p75, p90, p99 (with 0.1% resolution) |
| Burn cycles | Total recorded cycles |
| Median burn / pause | Median duration of burn and pause phases |
| Average burn / pause | Mean duration of burn and pause phases |
| Burner runtime | Total hours of burner operation |
| Ratio p90/max, p10/p50, p99-p90 | Modulation distribution coefficients |

### Schedule tab

24-hour heating schedule editor with hourly setpoint sliders.

---

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web dashboard |
| GET | `/api/status` | Full boiler state as JSON |
| GET | `/api/log` | Event log (latest 512 entries) as JSON |
| GET | `/api/stats` | Modulation statistics, percentiles, burn cycles, runtime as JSON |
| POST | `/api/control` | Set CH/DHW enable, setpoints, fault reset, timezone |
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

## Project Structure

```
esp-ot-gateway/
├── CMakeLists.txt          # ESP-IDF top-level build
├── sdkconfig.defaults      # Default build configuration
├── main/
│   ├── CMakeLists.txt      # Component definition
│   ├── main.c              # WiFi, NTP, task initialisation
│   ├── opentherm.c / .h    # OpenTherm protocol (GPIO ISR + esp_timer)
│   ├── http_server.c / .h  # HTTP server (esp_http_server)
│   ├── web_page.h          # Embedded HTML/CSS/JS dashboard
│   ├── log.c / .h          # Event log ring buffer (512 entries)
│   ├── stats.c / .h        # Modulation histogram, burn cycles, percentiles
│   └── wifi_config.h       # SSID / password
└── scripts/
    ├── setup.sh            # ESP-IDF installer
    ├── build.sh            # Build wrapper
    └── flash.sh            # Flash + monitor
```

---

## Debugging

```bash
# Monitor only (no reflash)
idf.py -p /dev/ttyUSB0 monitor
# Exit: Ctrl+]

# Check firmware size
idf.py size

# Size breakdown by component
idf.py size-components
```

### Erase NVS (factory reset)

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash
```

---

## Notes

- WiFi credentials are stored in plaintext in `main/wifi_config.h` — keep the device on a trusted local network.
- HTTP endpoints have no authentication; the dashboard is intended for LAN use only.
- The OpenTherm initialisation handshake (slave version exchange) runs at startup and repeats every 60 minutes to ensure DHW control stays active on some Baxi firmware versions.
- **Inter-frame gap**: A minimum 100 ms pause between OpenTherm transactions is required by the spec; violating this causes `NO_RESP` errors and boiler resets on Baxi hardware.
- **DHW setpoint (ID=56)**: The Baxi Duo-tec Compact does NOT accept DHW setpoint writes — it returns `NO_RESP` and uses its own internal setpoint (~60°C). Control DHW firing via `DHW_ENABLE` (bit 1) in the Status flags instead, paired with hysteresis in firmware.
- **CH2 enable**: Despite `SlaveConfig` reporting CH2=0, the Baxi requires `CH2_ENABLE` (bit 4) in the Status flags to switch the 3-way valve to the indirect DHW tank. Without CH2_ENABLE the valve stays on CH.
- **Modulation floor**: When the burner is active at minimum output the boiler reports `0x0000` (0% modulation). The firmware converts this to 0.3% for accurate percentile calculations.
- **Burner runtime tracking**: Uses `esp_timer_get_time()` (microseconds) — divide by 1000 for ms, then by 1000 for seconds. `time_t` on ESP32 is 8 bytes; do not cast `uint32_t*` to `time_t*`.
- **Relay**: The SmartTherm board relay (GPIO 23, HF33F-005-ZS3) is set HIGH at boot, closing the NO contact. On power loss the coil de-energises and the contact opens automatically — no shutdown code needed.
- **Build**: Always source the ESP-IDF environment first: `source ~/esp/esp-idf/export.sh && idf.py build`. Do not run `idf.py build` in the background (it may hang).
