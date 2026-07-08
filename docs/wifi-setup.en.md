# WiFi Setup Guide

**English** | [Русский](wifi-setup.md)

> ⚠️ **Maintainer note:** sections marked `TODO(verify)` describe expected behavior — check them against the current firmware before publishing.

## The blue LED: what the blinking means

The blue onboard LED (GPIO 2 on typical ESP32 devkits) is the network status indicator. **Blinking after power-on is normal** — it means the device has no established WiFi connection yet:

| LED behavior | Meaning |
|---|---|
| Fast blinking | Setup access point `ot-gateway-setup-XXXXXX` is active, waiting for configuration |
| Slow blinking | Connecting to the saved WiFi network |
| Off / steady `TODO(verify)` | Connected — normal operation |

`TODO(verify): exact blink rates and the connected-state indication.`

If the LED **keeps blinking for minutes** after start, the device can't reach your network: the router is down or out of range, the password changed, or the credentials were wiped. See [Troubleshooting](#troubleshooting) below — and note the boiler is unaffected either way: the fail-safe relay keeps it under the gateway's control, and OpenTherm control continues to work locally; only network features are waiting.

## First boot: the setup portal

On a fresh flash (or after a settings reset) the device has no WiFi credentials, so it starts its **own access point**:

- **SSID:** `ot-gateway-setup-XXXXXX` (the suffix is derived from the chip's MAC address, so it's unique per device)
- **Portal address:** `http://192.168.4.1` — most phones open the captive-portal page automatically right after connecting

From the portal you can choose one of two modes:

### Mode 1 — join your home WiFi (recommended)

1. Pick your network from the scan list (or type the SSID manually).
2. Enter the password and save.
3. The device reboots, connects to your router and the setup AP disappears.
4. Find the device's IP in your router's DHCP client list and open `http://<device-ip>`.

Tip: reserve a static DHCP lease for the device in your router — the dashboard address and MQTT behavior get much more predictable.

### Mode 2 — standalone access point

No router, no internet — the gateway keeps running its own AP permanently and serves the dashboard at `http://192.168.4.1`. Everything works except features that need the internet (NTP time sync; the 24-hour schedule then relies on time being unavailable — see the Info tab for sync status) and an external MQTT broker.

## Changing WiFi later

Open the dashboard → **WiFi tab**. You can rescan, switch networks, or switch between client and AP mode. Connection state and RSSI are shown live.

## Reconnection behavior

The firmware never gives up on WiFi:

- If the router disappears (power cut, reboot), the device retries forever with exponential backoff.
- If credentials become invalid or the network is gone for a long time, the device brings the setup AP back up so you can reconfigure it without reflashing. `TODO(verify): exact fallback timeout.`
- MQTT reconnects automatically after WiFi returns; Home Assistant discovery is re-published on every reconnect.

## Factory reset

- **From the dashboard:** WiFi tab → **"Reset all network settings"** (the red button at the bottom). Next to the status block there is also **"Switch to access point"** — it moves the device to standalone AP mode without wiping anything.
- **From a PC:** full NVS erase — this wipes WiFi credentials, MQTT settings, gas-meter calibration and schedule:

  ```bash
  idf.py -p /dev/ttyUSB0 erase-flash
  bash scripts/build_and_flash.sh /dev/ttyUSB0
  ```

## Troubleshooting

| Symptom | What to check |
|---|---|
| Blue LED keeps blinking after start | The device is still without a WiFi connection — either the setup AP is waiting for you (fast blink) or it can't reach the saved network (slow blink): router down, out of range, changed password |
| No `ot-gateway-setup-…` network after flashing | Give it ~30 s after boot; check serial monitor (`idf.py monitor`) for boot errors; confirm the flash completed without errors |
| Captive portal doesn't open automatically | Open `http://192.168.4.1` manually; disable mobile data on the phone so it doesn't route around the AP |
| Device joined WiFi but you can't find it | Look in the router's DHCP list for the hostname; or watch the serial log — the IP is printed on connect |
| Dashboard is slow / drops | Check RSSI on the WiFi tab; ESP32 antennas dislike metal boiler enclosures — move the board or use a board with an external antenna |
| Device falls off WiFi periodically | Set a DHCP reservation; check router logs; note that the firmware disables WiFi modem power-save for stability — if you changed that in menuconfig, change it back |
| Wrong password entered during setup | Wait for the setup AP to reappear, or do a factory reset |

## Security notes

- The dashboard and REST API are served over plain HTTP on your LAN and have no authentication — do **not** port-forward the device to the internet. If you need remote access, use a VPN into your home network (WireGuard, Tailscale, etc.).
- MQTT credentials are stored in NVS on the device. `TODO(verify): whether NVS encryption is enabled in the default sdkconfig.`
