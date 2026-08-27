# Validation checklist

## Automated

- Host, Zephyr ztest, and ESP Unity: `SVB1`/`SVW1`, IEEE CRC-32,
  malformed length, 16-bit wrap, golden vectors, and command replay cache.
- Bridge Rust tests: multiple reports, CRC rejection, discovery/offer encoding,
  datagram duplicate/reorder/boot handling, tracker registration, source
  timeout, cross-HID/Wi-Fi RF sequence deduplication, SlimeVR UDP encoding,
  and a socket-level handshake/sensor/rotation flow.
- Release builds: nRF54L15 receiver, ESP32-C5 gateway, Tracker XIAO nRF54L15,
  Bridge Linux/Windows, format and SPDX/license checks.

## Hardware acceptance

1. Confirm the ESP log prints `SPI link established` after both boards boot.
   A READY level alone is not sufficient; this log requires a successful
   request and response with valid envelope CRCs.
2. Run the nRF test-generator build for 30 minutes at 16 trackers × 100 Hz.
3. Record `spiTransferErrors`, `spiDecodeErrors`, ESP queue drops, receiver SPI
   CRC errors, and receiver report queue drops. Every value must remain zero.
   `reportsDiscarded` is a separate expected network-outage counter and must
   also remain zero while the Bridge is connected and ACKing.
4. On a stable LAN, capture generated and Bridge-accepted sequence counts.
   Delivery must be at least 99.9%, duplicate updates zero, and gateway-added
   latency p99 below 5 ms.
   On Windows, first confirm that the Private/LocalSubnet inbound UDP 6970
   Firewall rule described in the Bridge README is active.
5. Receive one real tracker simultaneously through a Bridge-owned USB Receiver
   and the Wi-Fi receiver. Only one device may be created. Disconnect each
   source independently; the tracker disconnects only after the final source
   ends.
6. Exercise 5 GHz non-DFS and DFS APs, invalid credentials, 30-second setup AP
   fallback, configuration reboot, DHCP address changes, Bridge/Server restart,
   dynamic discovery, fixed host, pairing, group change, clear, and nRF reboot.
7. Confirm setup AP is channel 1, normal station mode is 5 GHz-only, and RF
   reception remains paused while the setup AP is active.

Discovery is IPv4 subnet-local. Use the fixed Bridge field when broadcast or
client-to-client traffic is blocked.
