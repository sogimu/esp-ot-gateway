# MQTT & Home Assistant Integration

**English** | [Русский](mqtt.md)

> ⚠️ **Maintainer note:** topic paths and JSON field names below must match `MqttSocketAdapter` / the discovery generator exactly. Items marked `TODO(verify)` are placeholders — sync them with the code before publishing.

## Quick start with Home Assistant

1. Make sure the **Mosquitto broker** add-on (or any MQTT broker) is running and the **MQTT integration** is configured in HA.
2. On the gateway dashboard open the **MQTT tab** and fill in:
   - **Host / port** — broker address (for the HA add-on: the HA host IP, port `1883`)
   - **Username / password** — a broker login (create a dedicated HA user for the device)
   - **Prefix** — base topic, default `esp-ot-gateway`
   - **Status / statistics intervals** — how often the device publishes (see below)
3. Save. Within seconds the connection indicator goes green, discovery is published, and a new device with **27 entities** appears in HA → Settings → Devices & Services → MQTT.

No YAML is needed. If entities don't appear, publish an empty message to the rediscovery command topic (below) or press *Re-announce* on the MQTT tab.

## Connection behavior

- **MQTT 3.1.1**, QoS 0, keepalive 60 s, stable client ID derived from the chip MAC.
- **Availability (LWT):** the device registers a retained `offline` will and publishes a retained `online` birth message on every connect — HA marks entities unavailable the moment the broker loses the device.
- **Reliability:** zero-allocation client, PINGREQ keepalive, silent-disconnect watchdog, automatic reconnect with an escalating schedule — **5 → 10 → 20 → 40 → 60 s, and after 5 straight failures a retry every 5 min**. HA discovery configs are re-published after every reconnect.
- **TLS:** optional, off by default (toggle on the MQTT tab). The whole client can be disabled with the master MQTT switch on the same tab.
- **Intervals** are configurable from the MQTT tab and persist in NVS:
  - status: default **30 s**, range 5–3600 s
  - statistics: range 30–86400 s

## Topics

All topics live under the configurable prefix (default `esp-ot-gateway`).

| Topic | Direction | Retain | Payload |
|---|---|---|---|
| `<prefix>/availability` `TODO(verify)` | device → broker | yes | `online` / `offline` (LWT) |
| `<prefix>/status` `TODO(verify)` | device → broker | no | JSON: temperatures, flame, modulation, CH/DHW state, DHW prediction |
| `<prefix>/stats` `TODO(verify)` | device → broker | no | JSON: gas flow & consumption, modulation percentiles, burner cycles |
| `<prefix>/control` `TODO(verify)` | broker → device | — | JSON commands, see below |
| `<prefix>/cmd/ha_discovery` | broker → device | — | any payload → re-publish HA discovery (rate-limited to once per 10 min) |

### Control payload

Send a JSON object with any subset of these fields:

| Field | Type | Range | Meaning |
|---|---|---|---|
| `ch_enable` | bool | — | central heating on/off |
| `ch_setpoint` | number | 20–80 °C | CH flow temperature setpoint |
| `dhw_enable` | bool | — | domestic hot water on/off |
| `dhw_setpoint` | number | 35–80 °C | DHW setpoint |

Out-of-range values are clamped (additionally limited by the boiler-reported bounds from the Info tab — e.g. CH 25–80 °C, DHW 35–60 °C on the tested Baxi); malformed JSON is ignored and logged to the event journal.

Example — set heating to 55 °C and make sure it's on:

```bash
mosquitto_pub -h <broker> -u <user> -P <pass> \
  -t 'esp-ot-gateway/control' \
  -m '{"ch_enable": true, "ch_setpoint": 55}'
```

Watch everything the device says:

```bash
mosquitto_sub -h <broker> -u <user> -P <pass> -v -t 'esp-ot-gateway/#'
```

## The 27 Home Assistant entities

| Group | Count | Entities |
|---|---|---|
| Temperature sensors | 9 | CH supply, CH return, DHW tank, outdoor, room T1, room T2, … `TODO(verify: full list)` |
| Binary sensors | 5 | flame, CH active, DHW active, fault, … |
| System binary sensor | 1 | time synchronized (SNTP) |
| Switches | 2 | CH enable, DHW enable |
| Numbers | 2 | CH setpoint (20–80 °C), DHW setpoint (35–80 °C) |
| CH mode | 1 | manual / PID / schedule `TODO(verify: entity type)` |
| DHW prediction | 6 | time-to-ready, uncertainty, session metrics |
| DHW last session | 1 | summary of the previous DHW heat-up |

## Example automations

Turn heating down at night:

```yaml
alias: Night setback
trigger:
  - platform: time
    at: "23:00:00"
action:
  - service: number.set_value
    target:
      entity_id: number.esp_ot_gateway_ch_setpoint
    data:
      value: 45
```

Notify on boiler fault:

```yaml
alias: Boiler fault alert
trigger:
  - platform: state
    entity_id: binary_sensor.esp_ot_gateway_fault
    to: "on"
action:
  - service: notify.mobile_app_phone
    data:
      message: "Boiler reports a fault — check the Info tab for the ASF/OEM code."
```

*(Entity IDs above depend on your HA naming; copy the real ones from the device page.)*

## Monitoring the MQTT link itself

- Dashboard **MQTT tab** shows live connection state, last error and reconnect counters.
- REST: `GET /api/mqtt/status` returns the same as JSON (see [docs/api.en.md](api.en.md)).
- Broker side: the Mosquitto add-on log shows connects/disconnects of the client ID.
