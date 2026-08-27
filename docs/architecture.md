# Architecture

The MVP deliberately separates time-critical radio reception from network and
configuration work.

```text
Smol trackers --2.4 GHz RF v2--> XIAO nRF54L15
                                      |
                               SVB1 / SPI 2 MHz
                                      |
                                XIAO ESP32-C5
                                      |
                       SVW1 / 5 GHz Wi-Fi / UDP 6970
                                      |
USB HID receivers ----------------> Host Bridge
                                      |
                         SlimeVR UDP / localhost 6969
                                      |
                           Stock SlimeVR Server
```

The nRF application validates the unchanged 28-byte RF v2 packet, maps the
receiver-local tracker ID to its 48-bit address, and packs up to three records
into `SmolReportV1`. It does not expose USB, HID, UART console, or a network
stack. The ESP polls only while `READY` is asserted and forwards reports
without application-level retransmission.

The host Bridge keys devices by the tracker's 48-bit address. Its shared hub
accepts USB HID and Wi-Fi sources, then applies one global RF sequence gate.
If USB and Wi-Fi hear the same packet, the first copy wins. Accepted updates
are translated to standard SlimeVR UDP packets, with the 48-bit address used
as the logical device MAC.

The gateway is identified by its ESP Wi-Fi station MAC, not its current IP.
Its random boot ID separates restarts. The Bridge rejects duplicate/reordered
datagrams and stale packets from a retired boot ID.

## Failure behavior

- SPI report queue overflow drops real-time data; it never replays it. When
  the network is unavailable, the UDP task drains reports into a separate
  discard counter so network backpressure is not reported as an SPI failure.
- Management commands use a 16-bit request ID, three attempts, and an
  eight-entry response cache on the receiver.
- Three seconds without a Bridge ACK clears the dynamic Bridge selection and
  restarts discovery. A configured static host is resolved again instead.
- A configured station that cannot join 5 GHz within 30 seconds changes to a
  channel-1 2.4 GHz setup AP. Receiver RF is paused for the entire AP session.
- Configuration save is followed by reboot into 5 GHz-only station mode.

No encryption/authentication is added above the trusted LAN in MVP. OTA,
IPv6, subnet-crossing discovery, and a production PCB are out of scope.
