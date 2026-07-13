# ESP OpenTherm Gateway

**English** | [Русский](../README.md)

**Turn your gas boiler into a smart heating system — for the price of an ESP32.**

[![Tests](https://github.com/sogimu/esp-ot-gateway/actions/workflows/tests.yml/badge.svg)](https://github.com/sogimu/esp-ot-gateway/actions/workflows/tests.yml)
[![Coverage](https://img.shields.io/badge/coverage-report-blue)](https://sogimu.github.io/esp-ot-gateway/coverage/)

Most gas boilers spend their lives being switched on and off by a dumb relay thermostat: full blast, overshoot, cool down, repeat. Your boiler can do better — it speaks **OpenTherm**, a digital protocol that lets it modulate smoothly, report every temperature it measures, and tell you exactly what it's doing.

**esp-ot-gateway** is open-source ESP32 firmware that unlocks all of it:

- 🔥 **Smooth, modulating heating** — PID room-temperature control instead of on/off cycling: stable room temperature, fewer burner starts, less wear
- 💰 **Know your gas bill before it arrives** — live gas flow estimation (m³/h), daily/weekly consumption, calibration against your real gas meter
- 🏠 **Home Assistant out of the box** — MQTT autodiscovery publishes 29 entities: just point it at your broker and your boiler appears in HA
- 📱 **Control from any device** — a fast, dark-theme web dashboard served straight from the ESP32; no app, no account
- ☁️ **No cloud. Ever.** — everything runs on your LAN. No subscriptions, no servers to shut down, no data leaving your home
- 🚿 **Hot water, predicted** — a Kalman filter estimates when your DHW tank will be ready ("hot water in ~12 min")
- ⏰ **24-hour schedule** — hourly heating setpoints, synced via NTP with timezone support

> Tested daily on a **Baxi Duo-tec Compact 1.24** with a SmartTherm OpenTherm adapter. The firmware implements a full OpenTherm v2.2 master, so other OpenTherm boilers are expected to work — reports and pull requests are very welcome.

---

## Screenshots

| Live boiler diagram | Control: modes & setpoints |
|---|---|
| ![Status tab](images/status.webp) | ![Control tab](images/control.webp) |

| Virtual gas meter with reconciliation | MQTT settings |
|---|---|
| ![Gas meter tab](images/gas-meter.webp) | ![MQTT tab](images/mqtt.webp) |

<details>
<summary>More screenshots: event journal, Info, WiFi</summary>

![Journal tab](images/journal.webp)
![Info tab](images/info.webp)
![WiFi tab](images/wifi.webp)

</details>

---

## Why not just buy a smart thermostat?

| | Relay "smart" thermostat | Commercial OT thermostat | **esp-ot-gateway** |
|---|---|---|---|
| Boiler modulation | ❌ on/off only | ✅ | ✅ PID with your own tuning |
| Gas consumption tracking | ❌ | rarely | ✅ with meter calibration journal |
| Home Assistant / MQTT | via cloud | via cloud | ✅ fully local |
| DHW tank logic & prediction | ❌ | ❌ | ✅ |
| Price | $$ | $$$ | ESP32 + OT adapter |
| Firmware | closed | closed | open source, 400+ unit tests |

---

## What you need

| Component | Notes |
|---|---|
| ESP32 dev board | any common ESP32 DevKit (240 MHz, WiFi) |
| OpenTherm adapter | SmartTherm-style level adapter between ESP32 GPIO and the boiler's OT bus |
| Gas boiler with OpenTherm | tested: [Baxi Duo-tec Compact 1.24](https://shop.baxi.ru/products/duo-tec-compact-1-24) |
| *(optional)* 2 × DS18B20 | room temperature sensors for PID control (GPIO 15 / 26) |
| *(optional)* MQTT broker | e.g. Mosquitto in Home Assistant, for smart-home integration |

Wiring: OT TX → GPIO 4, OT RX → GPIO 16, fail-safe relay → GPIO 23 (closes on boot, opens on power loss — your boiler falls back to its own controls if the ESP32 dies).

---

## Get running in 15 minutes

1. **Flash the firmware**

   ```bash
   bash scripts/setup.sh                  # installs ESP-IDF + toolchain (Ubuntu)
   source ~/esp/esp-idf/export.sh
   bash scripts/build_and_flash.sh /dev/ttyUSB0
   ```

2. **Connect it to WiFi — no code editing needed.** On first boot the device opens a WiFi network **`ot-gateway-setup-XXXXXX`**. Connect with your phone, the setup page opens automatically (or go to `http://192.168.4.1`), pick your home WiFi *or* let the device run its own access point — it works fully offline too. The blinking blue LED just means "no WiFi connection yet" — it settles once the device is online ([details](wifi-setup.en.md)).

3. **Open the dashboard** at `http://<device-ip>` — and you're looking at your boiler's live state: flame, modulation, supply/return temperatures, 3-way valve position.

4. *(optional)* **Connect Home Assistant:** dashboard → **MQTT tab** → enter your broker's address, login and password → Save. The gateway announces itself via MQTT autodiscovery and your boiler shows up in HA as a ready-made device.

Detailed WiFi setup, troubleshooting and factory reset: **[WiFi Setup Guide](wifi-setup.en.md)**.

---

## Home Assistant integration

The gateway publishes **29 autodiscovered entities** — no YAML required:

- **Sensors (9):** CH supply, return, DHW tank, outdoor, room T1/T2, modulation %, and more
- **Binary sensors (5 + system):** flame, CH active, DHW active, fault, time-sync status
- **Controls:** switches for CH and DHW enable, numbers for CH setpoint (20–80 °C) and DHW setpoint, CH mode
- **DHW prediction (6):** remaining heating time and uncertainty for the current hot-water session
- **Journal (2):** live event stream (`event.journal`) and latest event (`sensor.last_event`)

Everything is also available over plain MQTT topics (JSON), so Node-RED, Zigbee2MQTT-style dashboards or your own scripts work just as well:

- device → broker: periodic **status** (default every 30 s, configurable 5 s–1 h) and **statistics** messages
- broker → device: a **control** topic accepting `ch_enable`, `ch_setpoint`, `dhw_enable`, `dhw_setpoint`
- `…/cmd/ha_discovery` — re-publish HA discovery on demand

The MQTT client was built for 24/7 embedded reliability: zero-allocation implementation, keepalive watchdog, automatic reconnection with backoff, discovery re-published on every reconnect. Username/password auth supported; all intervals configurable from the web UI and persisted across reboots.

Full topic reference: **[mqtt.en.md](mqtt.en.md)**.

---

## The dashboard

Eight tabs, served straight from the ESP32 (see the screenshots above):

- **Status** — live boiler diagram (burner, heat exchanger, pump, 3-way valve), every temperature the system knows, link indicators (controller / OpenTherm / SNTP / MQTT), DHW readiness prediction
- **Journal** — 256-entry event log with category filters (system / user / hardware / modes / boot); SNTP sync, MQTT connections and DHW heat-up sessions are all traceable
- **Statistics** — burner modulation percentiles (p1–p99), sampled only while the burner is firing over a rolling recent-history window (1% resolution), plus burn/pause cycle analysis and burner runtime hours — great for spotting an oversized boiler or short-cycling
- **Virtual gas meter** — instant flow (m³/h), cumulative volume, 1h/3h/12h/24h/7d averages, an estimation-error indicator, and a **reconciliation journal**: enter your physical meter reading and a Kalman-smoothed calibration coefficient K keeps the estimate honest (it survives reboots)
- **Control** — heating enable; mode **Manual** or **Adaptive (PID)**, plus a submode **Static** or **Scheduled** — so the 24-hour grid can drive either fixed flow setpoints or PID room targets; DHW tank (indirect cylinder) enable, setpoint and hysteresis; a big red boiler-off button
- **Info** — OEM/ASF fault codes, boiler-reported setpoint bounds, boiler firmware & OpenTherm versions, NTP servers and timezone, uptime
- **WiFi** — connection status and RSSI, switch-to-AP button, network settings reset
- **MQTT** — broker settings with a live status indicator and the reconnect schedule in plain sight

---

## For the engineers

Under the friendly UI is a codebase built to hexagonal-architecture discipline:

- **Full OpenTherm v2.2 master stack** — Manchester encoding/decoding via GPIO ISR + 500 µs hardware timer, with Baxi-specific quirks handled (CH2_ENABLE valve trick, rejected DHW setpoint writes, 100 ms inter-frame gap)
- **Domain / application / infrastructure separation** — the heating logic has zero ESP-IDF dependencies and runs on the host
- **400+ unit tests, 1000+ assertions** — run under sanitizers in CI, with a public coverage report
- **Crash diagnostics** — reset-reason logging, core dump to flash, offline backtrace decoding via `decode_crash.sh`
- **REST API** — every dashboard action is a JSON endpoint (`/api/status`, `/api/control`, `/api/stats`, `/api/schedule`, `/api/mqtt/*`, …); `curl` examples in [api.en.md](api.en.md)
- **Hardened networking** — infinite WiFi reconnect with exponential backoff, heap-fragmentation countermeasures, recovery ladder for 24/7 uptime

Build requirements: ESP-IDF v5.3.x. See **[Building & Debugging](build.en.md)**.

---

## Safety notice

This firmware drives real heating hardware. The relay is wired fail-safe (boiler reverts to standalone operation on power loss), and OpenTherm itself limits what a master can command — but **you** are responsible for your installation. Work on boiler wiring with the boiler powered off, respect your local regulations, and if in doubt, consult a heating professional. This project is not affiliated with or endorsed by Baxi.

---

## Project status & contributing

Actively developed and running in production on the author's own heating system through real winters. Especially wanted:

- **Compatibility reports** for non-Baxi OpenTherm boilers (open an issue with your model + what worked)
- Screenshots and dashboards from your setup
- Bug reports with serial logs — the event journal and crash diagnostics make them easy to capture

⭐ If this project saved you from buying a cloud thermostat, a star helps other boiler owners find it.

## License

This project is licensed under the [GNU General Public License v3.0](../LICENSE) — free, copyleft software: you may use, study, modify and redistribute the firmware, including commercially, provided derivative works stay under GPL-3.0 and their source is made available to recipients.

By submitting a contribution (pull request) you agree it is accepted under the same GPL-3.0 terms (see [CONTRIBUTING.en.md](CONTRIBUTING.en.md)).

See [LICENSING.en.md](LICENSING.en.md) for details and FAQ.
