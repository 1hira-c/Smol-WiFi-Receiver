# Prototype wiring and power

Both boards use 3.3 V logic. The SPI data pins are intentionally asymmetric;
move only the nRF ends of the three data jumpers when converting an earlier
D8/D9/D10-to-same-pin prototype:

| Signal | ESP32-C5 role | XIAO pin / ESP GPIO | nRF54L15 role | XIAO pin / nRF pin |
| --- | --- | --- | --- | --- |
| SCK | SPI controller clock | D8 / GPIO8 | SPIS20 SCK | D5 / P1.11 |
| MISO | controller input | D9 / GPIO9 | SPIS20 output | D4 / P1.10 |
| MOSI | controller output | D10 / GPIO10 | SPIS20 input | D1 / P1.5 |
| CS | controller chip select | D3 / GPIO7 | SPIS NCS | D3 / P1.7 |
| READY | input with pulldown | D2 / GPIO25 | active-high output | D2 / P1.6 |
| GND | common reference | GND | common reference | GND |

Keep these wires short and place a 100 nF decoupling capacitor close to each
module. The jumper-wire MVP uses SPI Mode 0 at 2 MHz. The ESP applies a 2 us
guard interval before the first clock and after the last clock while CS is
active. The nRF peripheral is configured to accept up to 8 MHz, but 8 MHz was
not reliable on the current flying-lead prototype. Four MHz was usable during
bring-up, but 2 MHz was retained for additional flying-lead margin; retest the
higher rates on a routed PCB.

After every transfer, the nRF holds READY low for at least 50 us. The ESP must
observe that low level followed by a new high level before immediately draining
another envelope. This prevents a stale READY-high from starting a transaction
before the peripheral has rearmed.

Do not use the former nRF D8/D9/D10 plus D3 mapping. nRF54L15 peripherals
cannot mix pins from different GPIO ports, and P2 serial pins have dedicated
signal assignments. That mapping mixed P2 data with P1 CS and the SPIS ignored
every transaction even though a debugger could observe all wire levels. SPIS20
on D1/D3/D4/D5 keeps all four SPI signals on P1; READY remains on P1 as well.

## Do not join the USB supplies

Never connect the two `5V`/VBUS pins. Do not connect both USB cables while a
3V3 link between boards is installed: that can back-power one regulator from
the other.

Use one of these arrangements:

1. For normal operation, use one adequately rated regulated 3.3 V supply for
   both `3V3` pins and common GND, with both USB cables unplugged.
2. For independent USB flashing/debug, remove the 3V3 inter-board/supply link,
   power each XIAO from its own USB connector, and connect only the five logic
   signals plus GND.
3. If a carrier board later distributes power, include explicit load switches
   or ideal-diode isolation and keep both VBUS nets separate.

Before attaching USB, verify continuity and ensure there is no connection
between the two VBUS pins. The nRF54L15 is flashed through the XIAO's onboard
SAMD11/SWD path; the ESP32-C5 is flashed through its own USB interface.

The pin assignments follow the official Seeed XIAO maps:

- <https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/>
- <https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/>

The nRF54L15 routing and timing constraints are documented by Nordic:

- <https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/gpio.html-concept_port_capabilities>
- <https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/_tmp/nrf54l15/autodita/spis/parameters.elec_spec.html>
