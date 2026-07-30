# REST API

**English** | [Русский](api.md)

Every action in the web dashboard is backed by a JSON endpoint, so anything the UI can do, `curl` can do too. The dashboard itself is the reference client (it polls the device every 2 s) — open it with browser DevTools → Network to see live request/response shapes.

> ⚠️ **Maintainer note:** routes and field names marked `TODO(verify)` are reconstructed from the dashboard's behavior — sync them with the HTTP handlers before publishing.

Base URL: `http://<device-ip>`. No authentication — LAN use only (see the security note in [wifi-setup.en.md](wifi-setup.en.md)). All responses are JSON; POST endpoints persist settings to NVS and survive reboots.

## Endpoint summary

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Live boiler state, link indicators, DHW prediction |
| GET | `/api/stats` | Modulation percentiles, burner cycles, runtime |
| GET | `/api/info` `TODO(verify)` | Diagnostics + device info (see the Info tab) |
| GET | `/api/log` `TODO(verify)` | Event journal (256 entries, categorized) |
| GET/POST | `/api/control` | Enables, mode & submode, setpoints, PID parameters |
| GET/POST | `/api/schedule` | The 24-hour grid used by the "Scheduled" submode |
| GET | `/api/gas` `TODO(verify)` | Virtual gas meter: flow, totals, averages, error estimate, coefficient K |
| POST | `/api/gas/…` `TODO(verify)` | Meter reconciliation actions (see below) |
| GET | `/api/mqtt/status` | MQTT connection state |
| GET/POST | `/api/mqtt/settings` | Broker configuration |
| GET | `/api/ota/status` | OTA state: slot, version, progress, pending |
| GET | `/api/ota/versions` | Available versions from the GitHub Pages catalogue |
| POST | `/api/ota/start` | Start OTA update to the given tag `{"tag":"v0.6.0"}` |
| POST | `/api/ota/rollback` | Manual rollback to the previous OTA slot |

## `GET /api/status`

```bash
curl -s http://192.168.0.37/api/status | jq
```

Everything the Status tab shows `TODO(verify: exact field names)`:

| Group | Fields |
|---|---|
| Temperatures | CH supply, CH return, DHW tank (БКН), outdoor, room T1, room T2, heat-exchanger ▲/▼ |
| Boiler state | burner on/off, modulation %, CH allowed/forbidden, DHW state (heating / waiting), fault flag |
| Hydraulics | 3-way valve position (`heating` / `dhw`), pump |
| Links | controller link, OpenTherm link, SNTP synced, MQTT connected |
| DHW | readiness prediction (`ready_in_s`, uncertainty), last heat-up duration |

## `GET/POST /api/control`

The Control tab maps to these fields. GET returns the full state; POST accepts any subset:

| Field | Type / values | UI element |
|---|---|---|
| `ch_enable` | bool | Отопление → Разрешение |
| `ch_mode` | `manual` \| `pid` `TODO(verify values)` | Режим: Ручной / Адаптивный (PID) |
| `ch_submode` | `static` \| `schedule` `TODO(verify values)` | Подрежим: Статичный / По расписанию |
| `ch_setpoint` | °C, clamped to boiler-reported bounds (Info tab; 25–80 °C on the tested Baxi) | Уставка CH (manual + static) |
| `pid.sensor` | `t1` \| `t2` | room sensor feeding the PID |
| `pid.target` | °C | room target (PID + static) |
| `pid.kp` / `pid.ki` / `pid.kd` | number `TODO(verify)` | PID gains |
| `dhw_enable` | bool | Бойлер БКН → Разрешение |
| `dhw_setpoint` | °C, clamped to boiler-reported bounds (35–60 °C on the tested Baxi) | Уставка БКН |
| `dhw_hysteresis` | °C (e.g. 2.0) | Гистерезис БКН |

The red **"Выключить котёл"** button is the emergency stop — `TODO(verify): its endpoint/field (e.g. POST /api/control {"boiler_off":true})`.

```bash
# Manual static: heating on, flow setpoint 35 °C
curl -s -X POST http://192.168.0.37/api/control \
  -H 'Content-Type: application/json' \
  -d '{"ch_enable":true,"ch_mode":"manual","ch_submode":"static","ch_setpoint":35}'

# Adaptive (PID) with the 24-hour schedule driving the room target
curl -s -X POST http://192.168.0.37/api/control \
  -H 'Content-Type: application/json' \
  -d '{"ch_enable":true,"ch_mode":"pid","ch_submode":"schedule"}'
```

Out-of-range values are clamped to the boiler-reported bounds from `/api/info`; malformed JSON is rejected and logged to the event journal.

## `GET/POST /api/schedule` — the 24-hour grid

Mode and submode are orthogonal: the grid is used whenever `ch_submode = schedule`, and its meaning follows the mode:

- **Manual + schedule** — each hourly value is the **CH flow setpoint** for that hour;
- **PID + schedule** — each hourly value is the **PID room-temperature target** for that hour, and the PID keeps modulating the boiler to hold it.

Hours are device-local time, so NTP + timezone must be configured (Info tab shows sync state; the Journal logs the SNTP sync).

GET response / POST body `TODO(verify: exact shape)`:

```json
{
  "entries": [19, 19, 19, 19, 19, 20, 21, 22, 22, 21, 21, 21,
              21, 21, 21, 21, 22, 22, 22, 22, 22, 21, 20, 19]
}
```

`entries[0]` is 00:00–00:59, `entries[23]` is 23:00–23:59 (the example above is PID room targets with a night setback).

```bash
curl -s http://192.168.0.37/api/schedule | jq
curl -s -X POST http://192.168.0.37/api/schedule \
  -H 'Content-Type: application/json' \
  -d '{"entries":[19,19,19,19,19,20,21,22,22,21,21,21,21,21,21,21,22,22,22,22,22,21,20,19]}'
```

POST replaces the grid atomically and persists it to NVS.

## Virtual gas meter

The Gas tab exposes, and the API mirrors `TODO(verify: routes)`:

- **Readings:** instant flow (m³/h), cumulative volume, 1h/3h/12h/24h/7d averages, **estimation error %**, current calibration coefficient **K**.
- **Reconciliation (Сверка):**
  - set the initial meter reading once ("Нач. показание" → Установить);
  - submit the current physical reading ("Показание счётчика" → Записать). A reconciliation is accepted only after enough gas has flowed since the last one (> 0.01 m³ and the accumulated volume is > 5% of consumption — the UI shows both counters);
  - each accepted reconciliation appends a journal entry (time, actual, computed, Δ, %, K before, K after) and updates the Kalman-smoothed coefficient K.
- **Resets:** gas statistics reset; reconciliation journal + K reset.

```bash
curl -s http://192.168.0.37/api/gas | jq
curl -s -X POST http://192.168.0.37/api/gas/correction \
  -H 'Content-Type: application/json' -d '{"meter_reading_m3": 1382.041}'   # TODO(verify)
```

## `GET /api/info`

Mirrors the Info tab: OEM fault code, ASF flags, OEM diagnostics, boiler-reported CH/DHW setpoint bounds, boiler firmware version, OpenTherm version, platform, timezone (UTC offset, editable), NTP servers 1/2 (editable), ESP32 time, uptime.

## OpenTherm Data-IDs

The values the gateway exchanges with the boiler over the OpenTherm protocol. R/W is derived from how the firmware uses each one: **R** — the gateway polls it (read), **W** — the gateway writes it. These Data-IDs are what back the fields in `/api/status`, `/api/stats` and `/api/info`.

| ID | Purpose | R/W | Notes |
|---|---|---|---|
| 0 | STATUS (status flags) | R/W | Master writes its flags (CH/DHW/CH2 enable), slave returns its own (fault, burner, flame, DHW) |
| 1 | CH flow setpoint | W | |
| 2 | Master configuration | W | |
| 3 | Slave configuration | R | |
| 5 | ASF fault flags | R | HB = flags, LB = OEM code |
| 8 | CH2 setpoint | W | Used for the DHW tank on system boilers |
| 17 | Modulation, % | R | |
| 18 | CH pressure | R | |
| 25 | CH flow temperature | R | |
| 26 | DHW temperature (tank) | R | |
| 27 | Outside temperature | R | |
| 28 | Return temperature | R | |
| 48 | DHW setpoint bounds | R | lo/hi bytes |
| 49 | CH setpoint bounds | R | lo/hi bytes |
| 56 | DHW setpoint | R | Writable per spec, but the tested Baxi rejects the write — see note below |
| 57 | Max CH setpoint | W | |
| 115 | OEM diagnostic | R | |
| 116 | Burner starts counter | R | |
| 117 | CH pump starts counter | R | |
| 118 | 3-way valve switches counter | R | |
| 119 | DHW burner starts counter | R | |
| 120 | Burner hours | R | |
| 121 | CH pump hours | R | |
| 122 | DHW valve hours | R | |
| 123 | DHW burner hours | R | |
| 124 | OT protocol version (slave) | R | |
| 125 | Slave firmware version | R | |
| 126 | Master firmware version | W | |

> ⚠️ **ID 56 (DHW setpoint).** Per the OpenTherm spec this is a writable parameter, but on the tested **Baxi Duo-tec Compact** the write is rejected (NO_RESP / `DATA_INVALID` response) and the boiler holds its own internal setpoint (~60 °C set from the front panel). That is why ID 56 is marked **R** (effectively read-only) in the table. Do not expect a successful write to ID 56; the gateway regulates DHW through the `DHW_ENABLE` flag in STATUS (ID 0) with hysteresis — the `dhw_setpoint` from `/api/control` is enforced by the gateway itself.

## `GET /api/log`

The 256-entry journal with the same categories as the UI filters: system, user, hardware, modes, boot. Timestamps before the first SNTP sync are shown as unknown (`??:??:??`) — the journal is still ordered.

## MQTT

```bash
curl -s http://192.168.0.37/api/mqtt/status | jq

curl -s -X POST http://192.168.0.37/api/mqtt/settings \
  -H 'Content-Type: application/json' \
  -d '{"enabled":true,"host":"192.168.0.67","port":1883,"username":"user","password":"secret",
       "prefix":"esp-ot-gateway","status_interval_s":15,"stats_interval_s":30,"tls":false}'
```

Settings match the MQTT tab: master enable, host, port, login, password, topic prefix, status interval (s), statistics interval (s), TLS toggle. Topic reference and Home Assistant details: [mqtt.en.md](mqtt.en.md).

## OTA — Over-the-Air Updates (since v0.6.0)

The firmware uses a dual-slot (A/B) scheme with two OTA slots (`ota_0`, `ota_1`). After flashing a new image the device reboots, and the new firmware must prove it is healthy within 90 seconds (HTTP server up + main loop ticking). If health is not confirmed, the bootloader rolls back to the previous slot.

```bash
# OTA state
curl -s http://192.168.0.37/api/ota/status | jq
# → {"state":"idle","progress":0,"current_version":"v0.6.0","target_tag":"","rollback_pending":false,"last_error":""}

# Available versions (from versions.json on GitHub Pages)
curl -s http://192.168.0.37/api/ota/versions | jq

# Start update
curl -s -X POST http://192.168.0.37/api/ota/start \
  -H 'Content-Type: application/json' -d '{"tag":"v0.6.0"}'
# → {"ok":true}

# Manual rollback to the previous slot (reboot — response arrives before restart)
curl -s -X POST http://192.168.0.37/api/ota/rollback
# → {"ok":true}
```

| `/api/ota/status` field | Type | Description |
|---|---|---|
| `state` | `idle`/`fetching`/`writing`/`verify_pending`/`done`/`error` | Current state |
| `progress` | 0–100 | Download progress |
| `current_version` | string | Version running in the current slot |
| `target_tag` | string | Version tag being updated to |
| `rollback_pending` | bool | Image is awaiting validity confirmation |
| `last_error` | string | Last OTA error |

Full test plan: **[ota-testing.md](ota-testing.md)**.

## Conventions

- Errors return a non-2xx status with `{"error":"..."}` `TODO(verify)`.
- Numeric setpoints are clamped to boiler-reported bounds, never rejected for being slightly out of range.
- The dashboard polls every **2 s** (shown on the Info tab) — treat that as the sensible polling floor; the device is a 240 MHz microcontroller that is also bit-banging OpenTherm.
