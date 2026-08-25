# Smol Wi-Fi Receiver

[日本語](README.ja.md)

Firmware for a two-MCU Wi-Fi receiver for the Smol Slime (SlimeVR Tracker nRF)
tracker family, made from a Seeed Studio XIAO nRF54L15 and a XIAO ESP32-C5.

- The nRF54L15 receives the existing RF v2 packets sent by Smol Slime trackers
  over 2.4 GHz; the Tracker RF format is not changed.
- The ESP32-C5 transfers receiver reports over 5 GHz Wi-Fi to SlimeVR Server.
- The two MCUs communicate over a 2 MHz SPI link with a READY handshake.

This repository intentionally targets the Wi-Fi receiver only. The existing
nRF52840 USB HID receiver remains in the upstream receiver repository.

## Layout

- `firmware/nrf54l15`: nRF Connect SDK v3.3.1 receiver application.
- `firmware/esp32c5`: ESP-IDF v6.0.2 gateway application.
- `protocol`: shared C wire formats and protocol documentation.
- `docs`: prototype wiring, provisioning, flashing, and validation notes.
- `tests/host`: host-buildable protocol tests.

## Quick start

Read [prototype wiring](docs/prototype-wiring.md), then build and flash both
firmwares using [the build guide](docs/build-and-flash.md). The gateway uses
5 GHz Wi-Fi exclusively during normal operation. If it is unconfigured or
cannot connect for 30 seconds, it opens `SmolReceiver-Setup-XXXX` on 2.4 GHz
with the initial WPA2 password `smol-wifi-setup`.

Additional references: [architecture](docs/architecture.md),
[wire protocols](protocol/PROTOCOL.md), [Web API](docs/web-api.md), and the
[validation checklist](docs/validation.md). The latest bench state and exact
resume steps are in [current hardware bring-up](docs/current-bring-up.md).
Canonical cross-language bytes are kept in
[`protocol/golden_vectors.json`](protocol/golden_vectors.json) and the matching
C test initializers in `protocol/golden_vectors.h`.

## License

Unless otherwise noted, this repository is dual-licensed under either the MIT
License or Apache License 2.0, at your option. Code adapted from the SlimeVR
nRF receiver retains its original copyright notices.
