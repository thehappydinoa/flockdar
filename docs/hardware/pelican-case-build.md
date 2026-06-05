# Hardware Build: Pelican Case Stationary Hub (Platform A)

## Bill of Materials

| Item | Part | Est. Cost | Source |
|---|---|---|---|
| Compute | Raspberry Pi 4B 4GB | ~$55 | Approved resellers |
| 2.4GHz scanner | Hak5 WiFi Coconut | $100 | hak5.org |
| 2.4/5GHz scanner | Alfa AWUS036ACH | $40 | Amazon |
| BLE | USB Bluetooth 5.0 adapter | $10 | Amazon |
| GPS + mesh | Muzi Works H2T (Heltec T114 V2 + GPS + antenna + battery) | ~$60 | muziworks.com |
| USB hub | Sabrent HB-R7U3 60W 7-port powered | $40 | Amazon |
| Battery | Zeee 12V 20Ah LiPo (or equiv.) | $60 | Amazon |
| Buck #1 (Pi) | Pololu D24V22F5 (5.1V/2.5A) | $10 | pololu.com |
| Buck #2 (hub) | Pololu D24V50F5 (5V/5A) | $15 | pololu.com |
| Fan (x2) | Noctua NF-A4x20 FLX 12V 40mm | $20 | Amazon |
| Enclosure | Pelican 1510 carry-on | $170 | pelican.com |
| Panel SMA | SMA female bulkhead, RG316 pigtail | $8 | Amazon |
| Panel USB-C | USB-C panel mount extension | $8 | Amazon |
| Panel switch | Waterproof rocker switch, 12V | $6 | Amazon |
| Fan grille (x2) | 40mm finger guard + grommet | $4 | Amazon |
| Pi heatsink/fan | Official Pi 4 cooler or Pimoroni Fan SHIM | $10 | |
| microSD | Samsung Pro Endurance 128GB | $25 | Amazon |
| Misc | Wire, XT60 connectors, terminals, zip ties | $15 | |
| **Total** | | **~$656** | |

---

## Power Architecture

```
12V LiPo (20Ah = 240Wh)
    ├── Buck #1: 12V → 5.1V/2.5A → Pi 4B (USB-C)
    └── Buck #2: 12V → 5V/5A → Sabrent powered hub
                              ├── WiFi Coconut (USB 3.0, ~2.5A)
                              ├── Alfa AWUS036ACH (~0.5A)
                              ├── USB BT adapter (~0.1A)
                              └── H2T via USB-C (~0.2A charging)

Pi USB-A ports (direct):
    └── H2T data connection (serial, low current)
```

**Do not power the Coconut through the Pi's USB ports.** The Coconut draws up to 2.5A; the Pi 4's USB budget is ~1.2A total.

### Power draw summary

| Component | Typical | Peak |
|---|---|---|
| Pi 4B 4GB | 6W | 8W |
| WiFi Coconut | 12W | 15W |
| Alfa AWUS036ACH | 1.5W | 2.5W |
| BT adapter | 0.5W | 0.5W |
| H2T | 0.5W | 1W (charging) |
| Fans (2x) | 0.5W | 0.5W |
| **Total** | **21W** | **27.5W** |

**Runtime at 21W average:** 240Wh ÷ 21W = **~11 hours**  
**Runtime at 27.5W peak:** 240Wh ÷ 27.5W = **~8.7 hours**

---

## Pelican 1510 Layout

```
┌─────────────────────────────────────┐
│  [Fan in]          [Fan out]        │  ← rear wall, grommeted holes
│                                     │
│  ┌──────────┐  ┌─────────────────┐  │
│  │  Pi 4B   │  │  WiFi Coconut   │  │
│  │ + cooler │  │                 │  │
│  └──────────┘  └─────────────────┘  │
│                                     │
│  ┌──────┐  ┌────┐  ┌─────────────┐  │
│  │ Buck │  │Buck│  │  Sabrent hub│  │
│  │  #1  │  │ #2 │  │             │  │
│  └──────┘  └────┘  └─────────────┘  │
│                                     │
│  ┌──────────────────────────────┐   │
│  │  12V LiPo 20Ah               │   │
│  └──────────────────────────────┘   │
│                                     │
│  ┌──────┐  ┌────────┐               │
│  │ H2T  │  │  Alfa  │               │
│  └──────┘  └────────┘               │
└─────────────────────────────────────┘

Panel (right wall):
  [LoRa SMA]  [USB-C charge in]  [Power switch]  [Fan grilles]
```

---

## Assembly Notes

### Cooling

The case must not be sealed during operation. Two 40mm fans: one pulling air in (bottom), one exhausting (top or rear). Drill two 42mm holes, line with grommets, mount fans with M3 screws. At 21W in a ~20°C ambient, this keeps the Pi core temperature under 60°C.

Run fans from the 12V rail directly — do not power through Pi GPIO (insufficient current). Noctua NF-A4x20 at 12V is quiet enough for indoor use.

### H2T placement

Mount the H2T inside the case with the SMA pigtail routed to a panel-mount SMA connector on the case wall. The whip antenna attaches externally. USB-C to Pi for serial data + trickle charge. The H2T's internal 2000mAh battery means it maintains GPS fix during Pi reboots.

### WiFi Coconut

Plug into the Sabrent hub's USB 3.0 port. The Coconut's antennas are internal — no external antenna routing needed. Mount with velcro or a 3D-printed bracket to prevent vibration damage.

### Alfa AWUS036ACH

Uses a RP-SMA connector. If you want the antenna external, add a second panel-mount RP-SMA. Otherwise the internal 2dBi antenna is sufficient for vehicle deployment where the case lid is open.

### Wiring

- Use XT60 connectors on the LiPo for easy battery swap
- Fuse the 12V rail at 5A between battery and buck converters
- Label all USB cables — the Coconut cable and hub power brick look identical

---

## Software Setup

```bash
# Install muninnd
curl -L https://github.com/<org>/muninnd/releases/latest/download/muninnd-linux-arm64 \
  -o /usr/local/bin/muninnd && chmod +x /usr/local/bin/muninnd

# Install systemd unit
muninnd install-service

# Edit config
nano /etc/muninn/config.toml

# Start
systemctl enable --now muninnd
```

### config.toml for Platform A

```toml
[node]
id   = "pi-pelican"
name = "Pelican Hub"

[modules]
enabled = ["coconut", "wifi", "ble", "meshtastic", "serial"]

[modules.coconut]
# auto-detected by USB VID/PID — no config needed

[modules.wifi]
interface = "wlan1"   # Alfa adapter; wlan0 = Pi built-in (management)

[modules.ble]
adapter = "hci1"      # dedicated BT dongle; hci0 = Pi built-in

[modules.meshtastic]
port = "/dev/ttyUSB0"  # H2T
channel = "flockdar"

[api]
listen = "0.0.0.0:8080"
tls    = false          # LAN only; set true + cert for WAN

[store]
path = "/var/lib/muninn/detections.db"
```

---

## Field Checklist

- [ ] LiPo charged (check voltage: 12.6V = full, 10.5V = cutoff)
- [ ] H2T has GPS fix before deploying (LED solid = fix acquired, ~60s cold start)
- [ ] Coconut interfaces visible: `iw dev | grep Interface | wc -l` should return 14+
- [ ] Daemon running: `systemctl status muninnd`
- [ ] Web UI accessible: `http://pi-pelican.local:8080`
- [ ] Fans running (touch case to feel airflow)
