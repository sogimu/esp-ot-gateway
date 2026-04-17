# ESP OpenTherm Gateway

WiFi-connected OpenTherm boiler controller for ESP32. Implements a full OpenTherm v2.2 master stack and serves a responsive web dashboard for remote monitoring and control of the **Baxi Duo-tec Compact** gas boiler.

---

## Features

- **Full OpenTherm v2.2 protocol** — Manchester encoding/decoding via GPIO ISR + 500 µs hardware timer
- **Embedded web dashboard** — Dark-theme single-page app served directly from ESP32 on port 80
- **REST API** — JSON endpoints for boiler state, control commands, and heating schedule
- **24-hour heating schedule** — Hourly CH setpoints driven by NTP time sync (configurable timezone)
- **DHW priority logic** — Automatic 3-way valve control with hysteresis for indirect hot water tanks
- **Fault monitoring** — ASF flags, OEM diagnostic codes, one-shot fault reset

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32 (240 MHz, 520 KB RAM, 4 MB flash, built-in WiFi 802.11 b/g/n) |
| Protocol | OpenTherm v2.2 (master role) |
| TX pin | GPIO 4 → SmartTherm adapter → boiler bus |
| RX pin | GPIO 16 ← SmartTherm adapter ← boiler bus |

### Tested Hardware

| Component | Detail |
|-----------|--------|
| Development board | [ESP32 development board](https://ozon.ru/t/91Jlr6E) |
| Boiler | [Baxi Duo-tec Compact 1.24](https://shop.baxi.ru/products/duo-tec-compact-1-24) |

### OpenTherm Adapter Circuit

The OpenTherm bus runs at 24 V and requires galvanic isolation from the ESP32's 3.3 V logic.

```
OT bus (+) ──[R1 470Ω]──┬── PC817 anode
                         │
                    PC817 cathode ──[R2 100Ω]── OT bus (−) = boiler GND

PC817 collector ──[R3 10kΩ]── 3.3V
PC817 collector ──────────────────── GPIO 16 (RX, INPUT_PULLUP)
PC817 emitter   ── ESP32 GND

GPIO 4 (TX) ──[R4 1kΩ]── NPN base (BC547 / 2N2222)
                          NPN collector ──[R5 47Ω]── OT bus (+)
                          NPN emitter   ── GND
```

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

The dashboard auto-refreshes every 2 seconds and displays:

| Parameter | Description |
|-----------|-------------|
| Burner | Active / Off |
| CH pump | Running / Stopped |
| DHW pump | Running / Stopped |
| Fault | None / FAULT |
| CH supply temp | °C |
| Return temp | °C |
| DHW temp | °C |
| Pressure | bar |
| Modulation | % |

Controls available:
- **CH enable** toggle + setpoint slider (20–80 °C)
- **DHW enable** toggle + setpoint slider (35–65 °C)
- **Fault reset** button
- **Timezone offset** selector
- **24-hour heating schedule** editor (hourly setpoints)

---

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web dashboard |
| GET | `/api/status` | Full boiler state as JSON |
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
