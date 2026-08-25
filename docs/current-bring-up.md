# Current hardware bring-up state

Last updated: 2026-08-25 (JST).

## Hardware link verified

- The corrected asymmetric SPI wiring is installed and verified. The ESP log
  reports `SPI link established: receiver=7f6dfb046fcb protocol=1`.
- The jumper-wire prototype is stable in SPI Mode 0 at 2 MHz. At 8 MHz the
  controllers completed 80-byte DMA transfers but the received envelopes were
  not reliable. Four MHz was usable during bring-up, but 2 MHz was retained for
  additional margin on the flying leads. A future PCB may retest the higher
  rates with controlled trace geometry.
- The ESP32-C5 watchdog failure is fixed. Transfers use DMA-capable buffers and
  `SPI_DMA_CH_AUTO`, and every poll loop yields to the idle task. An earlier
  corrected build ran for 467 seconds and 466,058 transfers without a watchdog
  reset, beyond the former approximately 100-second failure point.
- The nRF bridge uses SPIS20 with all four SPI signals on GPIO port 1. Its CS
  input has an internal pull-up so an ESP reset leaves the peripheral safely
  deselected.
- The ESP applies a 2 us manual-CS guard before and after every full-duplex
  transfer, satisfying the nRF54L15 SPIS CSN setup/hold requirement.
- The nRF holds READY low for at least 50 us after a transfer. Immediate ESP
  drains require an observed READY low-to-high rearm transition, eliminating a
  race in which stale READY-high could clock the next frame too early.

In the corrected generator run, the report counter advanced from 90,646 to
240,741 (150,095 reports) without any increase in `transfer_errors`, `decode`,
`drops`, `timeouts`, or `report_drops`. The generator's single `spi_crc` was an
initial boot/partial-frame event and did not increase during that window.

After restoring the normal RF receiver, two final ESP health samples were:

```text
transfers=418290 transfer_errors=0 reports=369118 decode=2 drops=0 timeouts=0
transfers=478312 transfer_errors=0 reports=369118 decode=2 drops=0 timeouts=0
nrf: rf=0 rf_crc=0 report_drops=0 spi_crc=0
udp: connected=0 reports=0 discarded=369118
```

The two fixed ESP decode errors occurred while the nRF image was being changed;
they did not increase after the normal receiver reconnected. The unchanged
report count confirms the generator is no longer installed. Because no Server
or Wi-Fi credentials were available, generated reports were deliberately
drained into the separate `udp.discarded` counter. This prevents a network
outage from backpressuring the SPI queue or being misreported as a bridge drop.

## Wi-Fi and Server link verified

- The gateway is provisioned for a 5 GHz network with country `JP`.
  `wifiConnected=true` and `setupAp=false`; credentials are not recorded here.
- Its current DHCP lease is `172.34.1.38` (not a fixed address). Broadcast
  discovery found the modified SlimeVR Server on UDP 6969 and completed
  `DISCOVER` / `OFFER` / `HELLO` / `ACK`; `serverConnected=true`.
- The powered Tracker's live `paired_addr` was read non-destructively through
  its CMSIS-DAP probe. Tracker ID 0 is assigned to group `FD3EB08EAE51`, so the
  nRF is now configured as a secondary for that group. The earlier COM4 group
  `A5046FE8A5F5` was not this Tracker's group and correctly received no RF.
- 48-bit Web API IDs now use the same canonical numeric order as the USB
  Receiver console. Group changes return `OK`, then automatically reboot the
  nRF after its SPI response.
- Zephyr NVS returns the number of bytes written when a value changes. The
  receiver storage layer now treats every non-negative `nvs_write()` result as
  success; previously only a same-value write (return value zero) appeared to
  succeed. Changing from `A5046FE8A5F5` to `FD3EB08EAE51` verifies the fix.
- The post-reboot sample reached 119,541 SPI transfers with all ESP error/drop/
  timeout counters and all fresh nRF CRC/drop counters at zero.

## Live Tracker path verified

- With the Tracker powered, the secondary automatically registered tracker 0
  at address `8443F4CD8BB1` and began forwarding reports end to end.
- In a six-second steady-state sample, `rfReceived` advanced from 802 to 913,
  `spiReports` from 869 to 992, and `reportsSent` from 779 to 902.
- `reportsDiscarded` remained fixed at 90. Those reports accumulated only
  during the receiver reboot and Server discovery window; it did not increase
  in the steady-state sample.
- SPI transfer errors, RF/SPI CRC errors, bridge/receiver queue drops, command
  timeouts, and decode errors all remained unchanged during the sample.
- The modified Server logged one shared Smol device `8443F4CD8BB1` from gateway
  MAC `1801CCA3BD10` and created sensor 0 as `LSM6DSV`.
- A subsequent Server-restart test exposed an intermittent false ACK timeout:
  the Gateway sampled `now` before receiving an ACK, while the ACK handler
  recorded a newer `last_ack`. Crossing a FreeRTOS tick boundary made the
  unsigned `now - last_ack` underflow. The loop now samples `now` after control
  reception.
- With the corrected ESP image, a 30-second seven-sample run stayed connected,
  sent 305 reports, held `reportsDiscarded` fixed, reported zero UDP receive
  errors, and kept ACK age at or below 95 ms. A second Server stop/restart also
  rediscovered the Server automatically and passed the same 30-second check.
  The post-restart traffic was the periodic Tracker-address registration stream;
  the earlier live sensor sample verifies the complete RF data path.

## Installed firmware

- CMSIS-DAP probe `4B719E61` has been restored from the generator to the final
  normal Smol RF receiver release image.
- `COM24` contains the final ESP gateway image. NVS was preserved.
- CMSIS-DAP probe `2B660E07` is the existing SlimeVR Tracker and was left
  untouched.

Current build SHA-256 values:

| Image | SHA-256 |
| --- | --- |
| nRF receiver `build-nrf54l15/merged.hex` | `f6e7208d1f5c6809e4b06c365ff1e5349b134568170a1ce5be2486f7c0fe63aa` |
| nRF generator `build-nrf54l15-generator/merged.hex` | `d408941d42c1eb4ecf0661881611e59f2f5614e8b410e5d44bdb739cfb2cb209` |
| ESP application `smol_wifi_gateway.bin` | `875499027d096255135fb50b9fc1e43f8016fe95cf71e4f85b228a5e19e4c5ca` |

The receiver's pre-flash RRAM and UICR backups remain in the ignored
`build-device-backups` directory. Their SHA-256 values are respectively
`7a22f6dd662224b230b64b763c3f2294db994ba01a0934941744525d87eb1473`
and `06f4af83fcd2659a91ad36d1bc18aa0504ef4f890668b4ba50e7f2587779ee28`.

## Completed software checks

- Shared C golden-vector and protocol tests pass with strict warnings enabled.
- Both the normal and deterministic-generator nRF54L15 images build with NCS
  v3.3.1, and the ESP32-C5 gateway builds with ESP-IDF v6.0.2.
- SlimeVR Server core's complete unit-test task passes, including the gateway
  codec, discovery, duplicate/reorder handling, source timeout, shared HID
  decoder, and receiver-hub tests.
- The modified Desktop source compiles and its Gradle test task succeeds.
- The modified Android source compiles and its debug unit-test task succeeds;
  the Android module currently contains no executable unit-test sources, so
  Gradle reports that test phase as `NO-SOURCE`.

## Remaining acceptance tests

The connected legacy USB Receivers use groups different from
`FD3EB08EAE51`, so a live cross-HID/Wi-Fi duplicate-source test has not yet
been performed. It should be done with a USB Receiver already assigned to the
same group, without overwriting the existing receivers' stored tracker tables.

The 30-minute generator soak, DFS cases, DHCP-change repetition, and live
cross-HID/Wi-Fi Server dedup test remain acceptance tests rather than completed
bench results. One Server stop/restart cycle has passed with the final Gateway
image.
