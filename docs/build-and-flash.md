# Build and flash

## nRF54L15 receiver

Use nRF Connect SDK v3.3.1 and its matching toolchain.

```sh
west init -l .
west update
west build -p always -b xiao_nrf54l15/nrf54l15/cpuapp firmware/nrf54l15
west flash
```

The application intentionally has no USB device, HID, UART console, or shell.
Logs use SEGGER RTT. Build the deterministic soak-test variant with:

```sh
west build -p always -b xiao_nrf54l15/nrf54l15/cpuapp firmware/nrf54l15 -- -DCONFIG_SMOL_TEST_GENERATOR=y
```

It pauses RF and generates 16 synthetic tracker streams at 100 Hz each.

On the current Windows/WSL development machine, the equivalent reproducible
builds are available as:

```sh
bash ./ncs-build.sh release
bash ./ncs-build.sh generator
```

Override `NCS_ROOT_MSYS`, `NCS_TOOLCHAIN_MSYS`, and `NCS_TOOLCHAIN_WSL` if nRF
Connect SDK Manager installed the workspace or toolchain bundle elsewhere.

## ESP32-C5 gateway

Use ESP-IDF v6.0.2. The checked-in `.idf-version` documents the required
version for IDF version managers. The firmware selects the XIAO ESP32-C5's
official 8 MiB flash size and a 3 MiB application partition.

```sh
cd firmware/esp32c5
idf.py set-target esp32c5
idf.py build
idf.py -p YOUR_PORT flash monitor
```

On Windows, the repository helper writes the exact bootloader, partition, and
application offsets without erasing NVS:

```powershell
.\esp-flash.ps1 -Port COM24
```

The ESP serial log performs a bidirectional `GET_STATUS` transaction after
boot. `SPI link established` confirms READY, all four SPI signals, envelope
CRC, command handling, and the return path. A `health spi={...} nrf={...}` line
is emitted every 30 seconds; `decode`, `drops`, `timeouts`, `rf_crc`,
`report_drops`, `spi_crc`, and `transfer_errors` should remain zero during a
clean bench test.

On first boot, join `SmolReceiver-Setup-XXXX` using `smol-wifi-setup`, open
`http://192.168.4.1/`, and save a 5 GHz SSID. The unit reboots and enforces
5 GHz-only station mode. The default regulatory country is `JP`; set the
actual deployment country before normal use.

## Tests

```sh
make -C tests/host test
west twister -T tests/zephyr -p native_sim
```

The ESP Unity test app is at `firmware/esp32c5/test_apps/protocol`; build it
with IDF and run it on an ESP32-C5 test target. Its menu contains the `[smol]`
tests. SlimeVR Server's core tests
contain the matching UDP codec, discovery, CRC, reorder, source timeout,
registration, and cross-source RF dedup coverage.
