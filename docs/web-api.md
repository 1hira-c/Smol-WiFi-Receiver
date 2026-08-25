# Gateway Web API

The HTTP service is available on the setup AP and on the assigned station IP.
MVP assumes a trusted LAN and does not authenticate these endpoints.

| Method and path | Purpose |
| --- | --- |
| `GET /api/status` | Wi-Fi, Server, SPI/RF counters, receiver/group state |
| `GET /api/trackers?page=0` | Eight receiver registrations per page |
| `POST /api/config` | Save Wi-Fi, country, and optional fixed Server; reboot |
| `POST /api/receiver/group` | Select primary/secondary and 48-bit group ID |
| `POST /api/receiver/pair/start` | Start pairing (primary only) |
| `POST /api/receiver/pair/stop` | Stop pairing |
| `POST /api/receiver/clear` | Clear registered trackers |
| `POST /api/receiver/reboot` | Reboot the nRF receiver |

Configuration body:

```json
{
  "ssid": "5GHz-network",
  "password": "secret",
  "country": "JP",
  "server": "",
  "serverPort": 6969
}
```

Leave `server` empty for broadcast discovery. Set an IPv4 address or hostname
when broadcast cannot cross the subnet or the AP uses client isolation.

Group body (the group ID is 12 hexadecimal digits in the same canonical,
most-significant-byte-first form printed by the USB Receiver `group`/`info`
console commands):

```json
{"secondary": true, "groupId": "112233445566"}
```
