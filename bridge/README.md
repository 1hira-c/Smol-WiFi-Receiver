# Smol Receiver Bridge

The Bridge is the single host-side owner of diversity receiver input. It
accepts both kinds of receiver simultaneously:

- one or more USB HID SlimeNRF receivers;
- one or more nRF54L15 + ESP32-C5 Wi-Fi receivers using SVW1.

Receiver-local tracker IDs are resolved to the tracker's 48-bit address. A
global RF sequence gate then forwards only the first copy of each broadcast to
an unmodified SlimeVR Server over its standard UDP tracker protocol.

```text
USB HID receivers ---------+
                           +--> Smol Receiver Bridge --> 127.0.0.1:6969
nRF54L15 + ESP32-C5 --SVW1-+                               SlimeVR Server
```

## Run

Install a current stable Rust toolchain, then run:

```sh
cargo run --release --manifest-path bridge/Cargo.toml
```

Defaults:

- SVW1 input: `0.0.0.0:6970`
- SlimeVR Server output: `127.0.0.1:6969`
- USB HID input: `1209:7690`

Windows Firewall must allow inbound UDP 6970 from the local subnet. From an
elevated PowerShell prompt, create a narrowly scoped rule once:

```powershell
.\bridge\windows-firewall.ps1
```

The helper creates only the Private-profile, LocalSubnet-scoped UDP 6970 rule
and leaves an existing rule unchanged.

Configure an ESP32-C5 gateway's Bridge port as `6970`. The device firmware
migrates the old dynamic-discovery default from `6969` to `6970` while
preserving fixed hosts and other explicitly selected ports. Confirm each
logical tracker in SlimeVR Server when it first reports its 48-bit address as
a MAC.

Useful options:

```text
--gateway-listen ADDRESS  SVW1 listen address
--server ADDRESS          SlimeVR Server output address
--hid VID:PID             additional/replacement HID product; repeatable
--no-hid                  Wi-Fi-only operation
```

## USB ownership

The upstream receiver PID `1209:7690` is also opened by SlimeVR Server. It is
usable for Bridge development, but an unmodified Server must not consume the
same physical reports or it will create a second HID device. The production
solution is a separately allocated PID for diversity/Bridge firmware. Do not
publish firmware with an unallocated experimental PID.

Legacy HID reports without RF-v2 sequence metadata are accepted, but they
cannot participate in reliable cross-receiver deduplication. Diversity HID
firmware must emit the metadata report described in `protocol/PROTOCOL.md`.

## Current translation coverage

Rotation, acceleration, sensor status, battery, voltage, temperature, RSSI,
and taps are translated. The stock UDP tracker protocol has no equivalent for
the HID raw magnetometer vector, battery runtime, or sleep deadline; those
fields are currently omitted.

## 日本語

BridgeはUSB HIDレシーバーとnRF54L15＋ESP32-C5 Wi-Fiレシーバーを同時に
受け付け、48-bitトラッカーアドレスとRF sequenceで一つのストリームへ統合します。
重複排除後のデータは未改造SlimeVR ServerのUDP 6969へ送信します。

Wi-Fi Gatewayの送信先ポートは6970に設定してください。USBについては、従来PID
`1209:7690`を未改造Serverも開くため、最終配布前にBridge専用PIDの正式割り当てが
必要です。また、複数受信機間の重複排除にはRF-v2 sequence metadataを出力する
ダイバーシティ対応HID FWが必要です。

Windowsでは、PrivateネットワークのLocalSubnetから受信するUDP 6970をFirewallで
許可してください。上記PowerShell例はポート・プロファイル・送信元を限定しています。
