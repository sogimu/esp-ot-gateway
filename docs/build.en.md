# Building & Debugging

**English** | [Русский](build.md)

> ⚠️ **Maintainer note:** items marked `TODO(verify)` must be checked against the actual scripts/CMake targets before publishing.

## Requirements

| | |
|---|---|
| OS | Linux (Ubuntu 22.04/24.04 tested). macOS works with a manual ESP-IDF install. Windows: use WSL2 + [usbipd](https://learn.microsoft.com/windows/wsl/connect-usb) for the serial port |
| ESP-IDF | **v5.3.x** (other majors are not guaranteed to build) |
| Disk / RAM | ~4 GB free for the toolchain, 4 GB RAM is enough |
| Hardware | ESP32 dev board on USB (shows up as `/dev/ttyUSB0` or `/dev/ttyACM0`) |

## Option A — one-command setup (Ubuntu)

```bash
bash scripts/setup.sh          # installs system packages, ESP-IDF v5.3.x and the toolchain into ~/esp
```

## Option B — manual ESP-IDF install

1. System packages:

   ```bash
   sudo apt-get update && sudo apt-get install -y \
     git wget flex bison gperf python3 python3-pip python3-venv \
     cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
   ```

2. Clone and install ESP-IDF:

   ```bash
   mkdir -p ~/esp && cd ~/esp
   git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf && ./install.sh esp32
   ```

3. Activate the environment (**every new shell**, or add an alias to `.bashrc`):

   ```bash
   source ~/esp/esp-idf/export.sh
   # handy alias:  echo "alias get_idf='. ~/esp/esp-idf/export.sh'" >> ~/.bashrc
   ```

## Serial port access

```bash
sudo usermod -aG dialout $USER    # then log out and back in
ls -l /dev/ttyUSB*                # board must be visible; also try /dev/ttyACM*
```

If flashing fails with *Permission denied*, it's this. If the port doesn't appear at all — try another cable (charge-only USB cables are the #1 time sink) or install the CP210x/CH340 driver for your board.

## Configure, build, flash

```bash
cd esp-ot-gateway
idf.py set-target esp32                    # once per fresh checkout
idf.py menuconfig                          # optional: see project options below
bash scripts/build_and_flash.sh /dev/ttyUSB0
# equivalent to: idf.py build && idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor             # serial log; Ctrl+] to exit
```

Useful extras:

```bash
idf.py size-components        # what eats flash/RAM
idf.py -p /dev/ttyUSB0 erase-flash   # full wipe: settings, WiFi creds, calibration
idf.py fullclean              # nuke the build dir if CMake cache goes weird
```

Project-specific `menuconfig` options (GPIO pins for OT TX/RX, relay, DS18B20; feature toggles): `TODO(verify): list the actual Kconfig section names.` Defaults match the wiring in the README (OT TX → GPIO 4, OT RX → GPIO 16, relay → GPIO 23, DS18B20 → GPIO 15/26).

## Project layout

```
domain/           heating logic, PID, DHW prediction, gas estimation — pure C++, no ESP-IDF
application/      use cases wiring domain objects behind ports (interfaces)
infrastructure/   adapters: OpenTherm GPIO driver, WiFi, HTTP server, MqttSocketAdapter, NVS, SNTP
test/            host-side unit tests for domain + application
scripts/          setup, build, flash, test and crash-decoding helpers
```

The point of this split: everything above `infrastructure/` compiles and runs **on your PC**, which is why the test suite doesn't need a devkit.

## Running the tests

The suite is 400+ tests / 1000+ assertions and runs on the host, not on the chip.

Host prerequisites: `gcc`/`g++` ≥ 11 (or clang ≥ 14), `cmake` ≥ 3.16, `ninja-build` — all already installed if you did Option B step 1.

```bash
bash scripts/build_and_flash.sh          # builds firmware + runs tests
```

Manual equivalent:

```bash
cmake -S test -B build_tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build_tests
./build_tests/run_tests
```

Useful selections:

```bash
./build_tests/run_tests -r Pid               # run tests matching a name
```

**Sanitizers** (CI runs these on every push — run them locally before a PR):

```bash
cmake -S test -B build_asan \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build_asan && ./build_asan/run_tests
```

**Coverage** (the CI publishes the report to GitHub Pages):

```bash
cmake -S test -B build_cov -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build_cov && ./build_cov/run_tests
gcovr -r . build_cov --html-details -o coverage.html
```

When adding a feature: put the logic in `domain/`, write the test first, keep the infrastructure adapter thin. PRs that add untested domain logic will be asked to add tests.

## Crash diagnostics

The firmware is instrumented for postmortem debugging:

- **Reset reason** is logged to the event journal on every boot (power-on, panic, task/interrupt watchdog, brownout).
- **Core dumps** are written to a dedicated flash partition on panic.
- Decode a crash offline:

  ```bash
  bash scripts/decode_crash.sh /dev/ttyUSB0    # TODO(verify): exact arguments
  ```

  This pulls the dump and symbolizes the backtrace against the ELF — keep the `build/` directory of the **flashed** version, symbols from a different build are useless.

A perfect bug report: decoded backtrace + event journal around the incident (Log tab) + boiler model.

## OpenTherm layer notes

- Manchester encoding/decoding runs in a GPIO ISR paced by a 500 µs hardware timer. Don't add blocking work to the OT task and keep other high-priority ISRs off that core.
- Baxi-specific quirks (CH2_ENABLE valve trick, rejected DHW setpoint writes, 100 ms inter-frame gap) live in a dedicated adapter. Another vendor's quirks belong in a sibling adapter, not in the protocol core.
- The fail-safe relay on GPIO 23 must stay "energized = ESP controls the boiler". Never invert it: a dead ESP32 must hand control back to the boiler.
