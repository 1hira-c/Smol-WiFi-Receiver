# Wire protocols

All integers are little-endian. CRC fields use CRC-32/ISO-HDLC (the standard
IEEE CRC-32 with initial and final XOR `0xffffffff`). CRC protects framing and
is not an authentication mechanism.

## BridgeEnvelopeV1

The ESP32-C5 is SPI controller and the nRF54L15 is SPI peripheral. Every
full-duplex transaction is exactly 80 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `SVB1` |
| 4 | 1 | version (`1`) |
| 5 | 1 | kind |
| 6 | 1 | flags (`MORE_PENDING=1`, `ERROR=2`) |
| 7 | 1 | payload length, 0–64 |
| 8 | 2 | direction-local sequence |
| 10 | 2 | command request ID, otherwise zero |
| 12 | 64 | payload, zero padded |
| 76 | 4 | CRC-32 over bytes 0–75 |

Kinds are idle, receiver report, command, command response, and asynchronous
status event. Real-time receiver reports are never retransmitted. Commands are
retried by request ID and command results are cached by the nRF firmware.

## SmolReportV1

The 64-byte report preserves the diversity-aware USB format:

- Bytes 0–47 contain three 16-byte tracker or registration records.
- Bytes 48–63 contain `FA FF D6 01`, valid tracker record count, then three
  `(sequence, RSSI magnitude, flags)` tuples, one reserved byte, and `6D`.
- Metadata flags are sequence-valid (`1`), RF-v2 (`2`), and CRC-valid (`4`).
- Registration records have type `255`, local tracker ID, and the 48-bit
  tracker address in bytes 2–7.

## GatewayDatagramV1

Gateway messages are received by Smol Receiver Bridge on its dedicated UDP
port (6970 by default). The Bridge forwards accepted logical tracker updates
to SlimeVR Server using the standard tracker UDP port.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `SVW1` |
| 4 | 1 | version (`1`) |
| 5 | 1 | kind: discover, offer, hello, ack, report batch |
| 6 | 1 | flags |
| 7 | 1 | report count, 0–8 |
| 8 | 6 | gateway ID (ESP eFuse base MAC) |
| 14 | 2 | reserved |
| 16 | 4 | random boot ID |
| 20 | 4 | datagram sequence |
| 24 | 2 | payload length |
| 26 | 2 | header length (`28`) |
| 28 | n | payload |
| 28+n | 4 | CRC-32 over header and payload |

The gateway broadcasts DISCOVER once per second until it receives OFFER.
After selecting a server it sends HELLO once per second; the server replies
with ACK. Three seconds without ACK causes automatic rediscovery unless a
static server target is configured.
