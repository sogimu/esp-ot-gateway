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
- **Gas flow estimation** — Estimates instant gas flow (m³/h) from input (nameplate) power and burner modulation, with Kalman filtering on modulation and return temperature, a warmup ramp after ignition, calorific value correction via Boyle's law (gas temperature = outdoor + offset), and multi-window EMA averaging (1h / 3h / 12h / 24h / 7d) with cumulative integral tracking. Efficiency is not in the flow formula — the gas meter measures input energy, not output
- **Gas meter correction journal** — Compare the estimated gas total to the physical meter reading. A correction coefficient (k_calib) is Kalman-smoothed across multiple corrections, preventing wild swings when gas consumption between readings is small. Corrections are rejected when the integral is less than 10% of actual consumption or when no gas has been consumed since the last correction. k_calib survives reboots. Logs up to 10 correction entries with timestamps
- **Fault monitoring** — ASF flags, OEM diagnostic codes, one-shot fault reset
- **Event log** — 256-entry ring buffer with 5 categories (System, User, Equipment, Mode, Boot) and real-time filtering in the web UI
- **MQTT client** — Zero-allocation MQTT 3.1.1 over raw TCP socket (~300 lines, no malloc). MAC-based fixed client ID ensures session takeover on reboot (no ghost sessions). Graceful disconnect with DISCONNECT packet + drain before close. Publishes boiler status (~1KB JSON, QoS 0) every 5–3600s (configurable via web UI), statistics (~2KB JSON, QoS 0) every 30–86400s, and availability (LWT/birth: `online`/`offline`, retained). PINGREQ every 20s keeps connection alive even during startup load spike (HA discovery of 27 entities). Subscribes to control commands and HA discovery trigger. Replaces ESP-IDF's built-in MQTT client which had confirmed heap leaks in outbox management
- **Home Assistant auto-discovery** — Publishes 27 MQTT discovery configs incrementally (one per poll cycle, non-blocking): 9 temperature sensors, 8 binary sensors (flame, fault, CH/DHW active, connected, DHW prediction, SNTP sync), 2 switches (CH/DHW enable), 2 numbers (CH/DHW setpoint), 6 DHW prediction sensors. Entities auto-appear in HA MQTT integration. Manual re-trigger via `cmd/ha_discovery` topic. Re-publishes on every MQTT reconnect to keep HA in sync after broker restart
- **Crash diagnostics** — Reset reason detection on every boot, core dump saved to flash on panic, backtrace decoded offline via `decode_crash.sh`
- **Modulation statistics** — 1000-bin histogram (0.1% resolution), percentile analysis (p1–p99), burn cycle tracking (256-entry ring), median/avg burn & pause times, burner runtime hours — on the Statistics tab and `/api/stats`
- **Relay control** — GPIO 23 relay closes at boot (normal operation) and opens on power loss (fail-safe)

---

## Recent Improvements

- **Recovery ladder** — 4-level self-healing: L1/L2 warn, L3 restarts HTTP server, L4 reboots device. Prevents silent death from heap fragmentation
- **WiFi infinite reconnect** — exponential backoff (5s→10s→20s→40s→60s cap), never gives up. No more 10-retry limit
- **WiFi power save disabled** — `WIFI_PS_NONE` enforced on every STA start to prevent lwIP buffer accumulation
- **MQTT ghost sessions fixed** — MAC-based permanent client ID prevents duplicate sessions on broker restart; graceful disconnect (DISCONNECT → shutdown → drain → close) tells broker we're leaving cleanly
- **MQTT keep-alive** — PINGREQ every 20s (not keepalive-5=55s) survives startup load spikes from HA discovery
- **MQTT socket cleanup** — SO_LINGER with RST on close eliminates lwIP TCP buffer leaks during WiFi outages (dead sockets only, not intentional disconnects)
- **MQTT intervals** — save_mqtt_intervals now correctly persists `mqtt_sti`/`mqtt_ssi` to NVS
- **Web UI** — tab polling only on active tab reduces HTTP load; fetch timeout 15s for fast disconnect detection; red status indicators on backend failure; unified "Save" button labels
- **Build** — `scripts/build_and_flash.sh` always fullclean + mandatory host tests to prevent stale LWIP/sdkconfig cache

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

### LED Indicators

The built-in LED (GPIO 2) on the ESP32 development board provides status information during startup:

| LED state | Meaning |
|-----------|---------|
| **Rapid blinking** (~5 Hz, on for 100ms, off for 100ms) | **NTP time sync in progress.** The controller is connecting to internet time servers to set the clock. This happens during every boot in WiFi Router mode and lasts up to 30 seconds |
| **LED off** | **Time sync complete** (NTP succeeded) **or timed out** (no internet). If the LED stays off immediately after boot, the controller is in AP mode and NTP sync was skipped — use manual time setting via the web interface |
| **Solid on / dim** | Normal operation — the LED is not used after startup. On some ESP32 boards the LED may glow dimly due to shared GPIO with internal circuitry |

> **Note:** On the ESP32 DevKit board, GPIO 2 is connected to the on-board blue LED. This is the same GPIO used by some boards for boot mode selection — do not connect external pull-down resistors to it.

### Tested Hardware

| Component | Detail |
|-----------|--------|
| Development board | [ESP32 development board](https://ozon.ru/t/91Jlr6E) — [user guide](docs/userguideSmartOT_01e.pdf) |
| Boiler | [Baxi Duo-tec Compact 1.24](https://shop.baxi.ru/products/duo-tec-compact-1-24) |

---

## Quick Start (Ubuntu)

### 1. Install ESP-IDF

```bash
bash scripts/setup.sh
```

This installs:
- System packages (`git`, `cmake`, `python3`, `ninja`, ...)
- **ESP-IDF v5.3.2** into `~/esp-idf/esp-idf`
- Xtensa toolchain (`xtensa-esp32-elf-gcc`)

### 2. Activate the ESP-IDF environment

```bash
source ~/esp/esp-idf/export.sh
```

> Add this line to `~/.bashrc` to avoid running it on every session.

### 3. Build

```bash
bash scripts/build.sh
# or directly:
idf.py build
```

### 4. Flash

```bash
bash scripts/flash.sh /dev/ttyUSB0
# or:
idf.py -p /dev/ttyUSB0 flash
```

### 5. First-time WiFi setup

After flashing, the controller starts in **setup mode**. Connect your phone or laptop to the WiFi network named **`Baxi-OT-Setup-XXXXXX`** (no password). Your browser should open automatically with the setup page. If not, open `http://192.168.4.1` manually.

Follow the on-screen instructions to choose your connection method (home WiFi router or own access point). See **[WiFi Setup Guide](#wifi-setup-guide)** for detailed instructions.

> **After setup,** if you chose "WiFi Router" mode, the controller reboots and the **blue LED blinks rapidly** while it syncs the clock with internet time servers (NTP). This takes up to 30 seconds. When the LED stops blinking, the controller is ready.

### 6. Open the web dashboard

Once connected, navigate to `http://<device-ip>` (the IP is assigned by your router for WiFi mode, or `192.168.4.1` for AP mode).

---

## WiFi Setup Guide

The controller supports two network modes. You choose one during initial setup and can switch at any time via the web interface.

### Connection Modes

| Mode | How it works | Web dashboard | Internet needed |
|------|-------------|---------------|-----------------|
| **WiFi Router** (STA) | Controller connects to your home WiFi network, just like your phone or laptop | Accessible at the IP address assigned by your router (e.g. `192.168.1.42`) | Yes — for NTP time sync. The controller works without internet, but the clock will be wrong |
| **Own Access Point** (AP) | Controller creates its own WiFi network. You connect your phone to this network when you want to check or adjust settings | Always at `192.168.4.1` | No — the controller works fully offline. Set the clock manually via the web interface |

### First-Time Setup

When you flash the controller for the first time (or after a factory reset), it starts in **setup mode**:

1. **Power on the controller.** It creates an open WiFi network named `Baxi-OT-Setup-XXXXXX` (the last 6 characters are unique to your device).
2. **Connect your phone or laptop to this network.** No password is needed.
3. **Your browser should open automatically** with the setup page. If it doesn't, open your browser and go to **`http://192.168.4.1`**.
4. **Choose your connection method** on the setup page:
   - **"Connect to my WiFi router"** — select this if you have a home WiFi network and want the controller always accessible on it.
   - **"Own access point"** — select this if you don't have a WiFi router, don't want the controller on your home network, or want a fully offline setup.
5. **Fill in the details** and tap **"Save & Reboot"**.

#### Option A: Connect to WiFi Router

1. Select **"Connect to my WiFi router"**
2. Tap **"Scan for Networks"** — wait a few seconds for the list to appear
3. Tap your home WiFi network in the list. Its name will appear in the input field
4. Enter your WiFi password (minimum 8 characters)
5. Tap **"Save & Reboot"**
6. The controller will restart and connect to your router. After reboot, open the web dashboard at the new IP address (check your router's device list, or look at the serial monitor: `idf.py -p /dev/ttyUSB0 monitor`)

#### Option B: Own Access Point

1. Select **"Own access point (no router needed)"**
2. Enter a password for the access point (minimum 8 characters). **Remember this password — you'll need it to connect**
3. Tap **"Save & Reboot"**
4. After reboot, the controller creates a WiFi network `Baxi-OT-Setup-XXXXXX` protected by your password
5. Connect your phone to this network and open `http://192.168.4.1`

### Accessing the Web Dashboard

**WiFi Router mode:** use the IP address assigned by your router. You can find it by:
- Looking at the serial output during boot: `WiFi подключён. IP: 192.168.x.x`
- Checking your router's DHCP client list
- The IP is displayed on the WiFi tab of the web interface

**AP mode:** always `http://192.168.4.1`. The controller's WiFi network name is `Baxi-OT-Setup-XXXXXX`.

> **Tip:** Bookmark the dashboard address in your browser. If the controller switches between modes, the bookmark for AP mode always works, while the STA mode address depends on your router.

### Switching Between Modes

You can change the network mode at any time from the WiFi tab in the web dashboard:

- **From WiFi Router to AP:** open the WiFi tab → tap **"Switch to Own Access Point"** → enter a new AP password → confirm. The controller reboots into AP mode.
- **From AP to WiFi Router:** open the WiFi tab → tap **"Connect to WiFi Router"** → this resets network settings and reboots into setup mode. Follow the first-time setup steps to configure a WiFi network.

### Setting the Clock (AP Mode)

In AP mode, the controller has no internet access and cannot sync time automatically. You can tell NTP sync was skipped because **the blue LED (GPIO 2) stays off** immediately after boot — no blinking occurs. The clock is important for:
- Timestamps in the event log
- The heating schedule (if you use time-based CH control)
- Gas consumption statistics with correct dates

**To set the clock manually:**
1. Open the web dashboard
2. Go to the **Info** tab
3. Find the **Time** section — it shows the current time and source (`manual` or `none`)
4. Enter the current date and time, tap **Apply**
5. The time is saved and survives reboots

When the controller switches to WiFi Router mode and connects to the internet, it will automatically sync time via NTP and override the manual setting.

### Troubleshooting

#### I can't see the "Baxi-OT-Setup-XXXXXX" WiFi network

- Make sure the controller is powered on (the ESP32 board has a power LED)
- The setup network is only active during **first boot** or after a factory reset. If the controller was previously configured, it won't appear
- Try moving closer to the controller — the WiFi range is limited
- The network name contains 6 hex digits unique to your device (e.g. `Baxi-OT-Setup-A1B2C3`)

#### The browser didn't open automatically (captive portal didn't work)

This depends on your phone's operating system and version. The automatic popup doesn't work on all devices.

- **Android:** open Chrome, type `192.168.4.1` in the address bar
- **iPhone/iPad:** open Safari, type `192.168.4.1` in the address bar
- **Windows laptop:** open any browser, type `192.168.4.1`
- If the page doesn't load, disconnect from other WiFi networks first, then reconnect to `Baxi-OT-Setup-XXXXXX`

#### The controller won't connect to my WiFi router

- **Wrong password:** the controller retries indefinitely with exponential backoff (5s→10s→20s→40s→60s cap). After 3 failed boot attempts with wrong credentials, it automatically returns to setup mode (open AP) so you can reconfigure.
- **Router too far:** the ESP32 has a small antenna. Move the controller closer to the router during initial setup.
- **5 GHz networks:** the ESP32 only supports **2.4 GHz WiFi**. If your router uses the same name for both bands, the controller should connect to the 2.4 GHz one automatically. If your router only broadcasts 5 GHz, the controller cannot connect.
- **Hidden SSID:** if your router doesn't broadcast its network name, it won't appear in the scan results. Type the network name manually in the input field.
- **MAC address filtering:** if your router restricts connections by MAC address, add the controller's MAC (shown in the serial monitor at boot) to your router's allowlist.

#### I forgot my AP password

If the controller is in AP mode and you forgot the password:

1. You can't recover it — the password is not displayed anywhere for security
2. You need to perform a **factory reset of network settings** to return to setup mode
3. To do this: power off the controller, then power on while holding... *(if a hardware reset button exists)*
4. **Without a reset button:** use `idf.py erase-flash` from a computer, then `idf.py flash` to reinstall the firmware. This erases all settings including boiler configuration and statistics

> **Important:** write down your AP password and keep it in a safe place. There is no password recovery mechanism.

#### The controller keeps rebooting

This is the **reboot-loop protection**. If the controller cannot connect to the configured WiFi network after 3 consecutive boot attempts, it automatically resets to setup mode (open AP). This prevents the controller from being permanently unreachable.

1. Connect to `Baxi-OT-Setup-XXXXXX` (no password)
2. The setup page opens — reconfigure your WiFi settings
3. If the problem persists, check that your WiFi router is powered on and broadcasting a 2.4 GHz network

#### The controller works but I can't access the web dashboard

- **WiFi Router mode:** the controller may have received a new IP address from your router. Check your router's DHCP client list. Open the WiFi tab (if you can access the controller via AP mode) to see the current IP.
- **AP mode:** make sure your phone is connected to `Baxi-OT-Setup-XXXXXX`, not to another WiFi network. The dashboard is at `http://192.168.4.1`.
- **Both modes:** the web server runs on port 80. Make sure you're using `http://` not `https://`.

#### Factory Reset (Network Settings Only)

To erase all WiFi settings and return to first-time setup mode:

1. Open the web dashboard
2. Go to the **WiFi** tab
3. Tap **"Forget All Network Settings"**
4. Confirm the dialog
5. The controller reboots into setup mode — the open AP `Baxi-OT-Setup-XXXXXX` appears

> This only erases WiFi credentials. Boiler configuration, statistics, and schedules are preserved.

#### Full Factory Reset (All Settings)

To erase everything including boiler settings, statistics, and schedules:

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash
```

> This completely wipes the device. All configuration will be lost.

---

## Web Dashboard

The dashboard auto-refreshes and features 7 tabbed views:

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

### Gas Meter tab

The gas flow is estimated from a physical boiler model: instant flow (m³/h) = k × input_power_kW / calorific_value. Input (nameplate) power is computed from burner modulation relative to configured Pmin/Pmax, so efficiency is not needed in the flow formula — the gas meter measures input energy. Calorific value is corrected for gas temperature via Boyle's law (T_gas = T_outdoor + offset). The result is multiplied by a calibration coefficient **K** refined through periodic meter corrections using a Kalman filter across recent corrections.

| Section | Description |
|---------|-------------|
| Gas flow | Instant flow (m³/h), cumulative integral (m³), and multi-window averages: 1h / 3h / 12h / 24h / 7d |
| Meter correction | Set the physical meter reading to compare against the estimated total. The correction coefficient K is Kalman-smoothed across multiple corrections, preventing wild swings when gas consumption between readings is small. K survives reboots. Correction journal shows actual reading, estimated total, difference, error %, and K before/after each entry |
| Boiler model | Configurable physical model parameters: CH power (Pmin/Pmax), DHW power, 3-point efficiency curve (T_return → η), and gas temperature offset |

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

### MQTT tab

- Enable/disable toggle with status indicator (green=connected, red=error, gray=off)
- Broker host, port, username, password, TLS toggle
- Topic prefix configuration (default: `esp-ot-gateway`)
- Configurable publish intervals: status (5–3600s, default 30s), statistics (30–86400s, default 300s)
- **MQTT reconnect**: 10s→10s→10s→60s×3→600s (10 min) exponential backoff. MAC-based fixed client ID ensures session takeover on reconnect — no ghost sessions
- Tooltips for all fields explaining each setting
- Save applies without reboot — clean disconnect + reconnect

### WiFi tab

- **Setup mode:** radio buttons to choose connection method (WiFi router or own AP), network scan with signal strength, manual SSID entry for hidden networks, password fields with validation
- **WiFi Router mode:** shows connected network name, IP address, signal strength (dBm). Button to switch to AP mode
- **AP mode:** shows access point name and IP (`192.168.4.1`). Button to switch to WiFi Router mode
- **Factory reset:** "Forget All Network Settings" button returns to first-time setup
- Auto-opens on first boot (captive portal detection)

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
| GET | `/api/wifi/status` | Current WiFi mode (sta/ap/first_boot), IP, SSID, RSSI |
| GET | `/api/wifi/scan` | Scan for nearby WiFi networks |
| POST | `/api/wifi/settings` | Save WiFi configuration and reboot |
| POST | `/api/wifi/forget` | Reset WiFi settings to first-boot mode and reboot |
| GET | `/prov` | Improv-compatible provisioning status (Home Assistant/ESP Web Tools) |
| POST | `/prov` | Improv-compatible: send WiFi credentials |
| GET | `/api/mqtt/status` | MQTT connection status (enabled, connected, host, port, user, prefix, tls) |
| POST | `/api/mqtt/settings` | Save MQTT configuration and reconnect |
| GET | `/api/system/time` | Current time (epoch, source: sntp/manual/none, timezone offset) |
| POST | `/api/system/time` | Set time manually (for AP mode without internet) |

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

# WiFi status
curl http://192.168.4.1/api/wifi/status

# WiFi scan (in AP mode)
curl http://192.168.4.1/api/wifi/scan

# Set time manually (in AP mode)
curl -X POST http://192.168.4.1/api/system/time \
  -H "Content-Type: application/json" \
  -d '{"epoch":1718496000}'

# Improv provisioning status
curl http://192.168.4.1/prov

# MQTT status
curl http://<device-ip>/api/mqtt/status

# MQTT settings (applies immediately, no reboot)
curl -X POST http://<device-ip>/api/mqtt/settings \
  -H "Content-Type: application/json" \
  -d '{"enabled":1,"host":"192.168.1.100","port":1883,"user":"","pass":"","prefix":"esp-ot-gateway","tls":0,"status_interval":30,"stats_interval":300}'
```

---

## MQTT Topics

All topics are prefixed with a configurable prefix (default: `esp-ot-gateway`).

### Published (device → broker)

| Topic | QoS | Retain | Frequency | Description |
|-------|-----|--------|-----------|-------------|
| `{prefix}/status` | 0 | no | Configurable (default 30s) | Full boiler state as JSON (~1KB) |
| `{prefix}/stats` | 0 | no | Configurable (default 300s) | Modulation histogram, burn cycles, gas flow (~2KB) |
| `{prefix}/online` | 1 | yes | On connect/disconnect | `online` (connected) / `offline` (LWT) |

### Subscribed (broker → device)

| Topic | QoS | Description |
|-------|-----|-------------|
| `{prefix}/cmd/control` | 0 | Boiler control commands (JSON, same fields as POST `/api/control`) |
| `{prefix}/cmd/ha_discovery` | 0 | Re-publish all Home Assistant discovery configs |

### Home Assistant Auto-Discovery Topics

Published on first connect (retained, QoS 0):

```
homeassistant/sensor/{prefix}_ch_temp/config       ← CH supply temperature
homeassistant/sensor/{prefix}_dhw_temp/config      ← DHW tank temperature
homeassistant/sensor/{prefix}_return_temp/config   ← Return temperature
homeassistant/sensor/{prefix}_outside_temp/config  ← Outside temperature
homeassistant/sensor/{prefix}_t1_temp/config       ← Room sensor T1
homeassistant/sensor/{prefix}_t2_temp/config       ← Room sensor T2
homeassistant/sensor/{prefix}_modulation/config    ← Burner modulation %
homeassistant/sensor/{prefix}_uptime/config        ← Uptime seconds
homeassistant/sensor/{prefix}_total_uptime/config  ← Total uptime seconds
homeassistant/binary_sensor/{prefix}_flame/config         ← Flame on/off
homeassistant/binary_sensor/{prefix}_fault/config         ← Boiler fault
homeassistant/binary_sensor/{prefix}_ch_active/config     ← CH active
homeassistant/binary_sensor/{prefix}_dhw_active/config    ← DHW active
homeassistant/binary_sensor/{prefix}_connected/config     ← Boiler connected
homeassistant/binary_sensor/{prefix}_dhw_pred_active/config ← DHW prediction active
homeassistant/binary_sensor/{prefix}_time_synced/config   ← SNTP time synced
homeassistant/switch/{prefix}_ch_enable/config            ← CH enable switch
homeassistant/switch/{prefix}_dhw_enable/config           ← DHW enable switch
homeassistant/number/{prefix}_ch_setpoint/config          ← CH setpoint (20–80°C)
homeassistant/number/{prefix}_dhw_setpoint/config         ← DHW setpoint (35–80°C)

// DHW prediction sensors (БКН)
homeassistant/sensor/{prefix}_dhw_pred_rate/config        ← DHW heating rate (°C/s)
homeassistant/sensor/{prefix}_dhw_pred_elapsed/config     ← DHW elapsed time
homeassistant/sensor/{prefix}_dhw_pred_remaining/config   ← DHW remaining time
homeassistant/sensor/{prefix}_dhw_pred_uncertainty/config ← DHW uncertainty
homeassistant/sensor/{prefix}_dhw_hyst_on/config          ← DHW hysteresis
homeassistant/sensor/{prefix}_ch_mode/config              ← CH mode (0-3)
homeassistant/sensor/{prefix}_dhw_last_session/config     ← DHW last session duration
```

### MQTT Control Examples

```bash
# Enable heating
mosquitto_pub -t "esp-ot-gateway/cmd/control" -m '{"ch_enable":1}'

# Set CH setpoint
mosquitto_pub -t "esp-ot-gateway/cmd/control" -m '{"ch_setpoint":65}'

# Disable DHW
mosquitto_pub -t "esp-ot-gateway/cmd/control" -m '{"dhw_enable":0}'

# Trigger HA discovery re-publish
mosquitto_pub -t "esp-ot-gateway/cmd/ha_discovery" -m ""
```

---

## Home Assistant Integration

### Setup

1. Add MQTT integration in HA: **Settings → Devices & Services → Add Integration → MQTT**
2. Enter broker address (e.g., `192.168.0.67`), port `1883`, and credentials (if required)
3. HA automatically discovers all 27 entities from retained discovery messages
4. Optional: trigger manual re-discovery via `mosquitto_pub -t "esp-ot-gateway/cmd/ha_discovery" -m ""`

### Configurable publish intervals

Set via web UI (MQTT tab) or REST API. Defaults: status every 30s, stats every 300s. Values are persisted in NVS and survive reboots.

### MQTT broker requirements

- Mosquitto 2.x: set `allow_anonymous true` or configure username/password in controller's MQTT tab
- HA Mosquitto add-on: default requires authentication (use credentials from Supervisor → Mosquitto → Configuration → `logins:`)

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
│   └── test_*.cpp                    # 409 tests, 1026 assertions
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

- WiFi credentials are stored in NVS (non-volatile storage) on the ESP32 flash. The device should be kept on a trusted local network.
- HTTP endpoints have no authentication; the dashboard is intended for LAN use only.
- **WiFi provisioning:** the controller supports two modes — connect to your home WiFi router, or create its own access point. Setup is done through a web interface. See **[WiFi Setup Guide](#wifi-setup-guide)**.
- The OpenTherm initialisation handshake (slave version exchange) runs at startup and repeats every 60 minutes to ensure DHW control stays active on some Baxi firmware versions.
- **Inter-frame gap**: A minimum 100 ms pause between OpenTherm transactions is required by the spec; violating this causes `NO_RESP` errors and boiler resets on Baxi hardware.
- **DHW setpoint (ID=56)**: The Baxi Duo-tec Compact does NOT accept DHW setpoint writes — it returns `NO_RESP` and uses its own internal setpoint (~60°C). Control DHW firing via `DHW_ENABLE` (bit 1) in the Status flags instead, paired with hysteresis in firmware.
- **CH2 enable**: Despite `SlaveConfig` reporting CH2=0, the Baxi requires `CH2_ENABLE` (bit 4) in the Status flags to switch the 3-way valve to the indirect DHW tank. Without CH2_ENABLE the valve stays on CH.
- **Modulation floor**: When the burner is active at minimum output the boiler reports `0x0000` (0% modulation). The firmware converts this to 0.3% for accurate percentile calculations.
- **Relay**: The SmartTherm board relay (GPIO 23, HF33F-005-ZS3) is set HIGH at boot, closing the NO contact. On power loss the coil de-energises and the contact opens automatically — no shutdown code needed.
- **DS18B20 sensors**: Two sensors are polled on a ~1.6 s cycle via software 1-Wire. Internal pull-up resistors (~45 kΩ) are used; readings are CRC-8 validated with automatic retry on failure. The polling task runs at FreeRTOS priority 4.
- **Crash recovery**: Core dumps are saved to a dedicated 704 KB flash partition. On the next boot the crash details (task name, PC, exception cause, backtrace) are logged to the event journal. Use `decode_crash.sh` to resolve addresses to source code locations.
- **Build**: Use `bash scripts/build_and_flash.sh` — runs fullclean build, host tests (409 tests), and flash in one command. Requires ESP-IDF 5.3.2 at `~/esp-idf/esp-idf`.
