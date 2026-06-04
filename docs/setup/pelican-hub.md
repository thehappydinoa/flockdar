# Setup: Pelican Hub (Platform A)

Raspberry Pi 4B + WiFi Coconut + Alfa + H2T. Maximum coverage stationary node.

## Prerequisites

```
Hardware ready:  Pi 4B assembled, Coconut connected to powered hub,
                 Alfa connected to hub, H2T connected via USB-C,
                 12V LiPo connected through buck converters
OS:              Raspberry Pi OS Lite (64-bit, Bookworm)
Network:         Pi on LAN (SSH accessible)
```

## 1. OS setup

```bash
# On Pi via SSH
sudo apt update && sudo apt upgrade -y
sudo apt install -y libpcap-dev bluez bluetooth
```

## 2. WiFi Coconut

```bash
# Verify all 14 interfaces appear
iw dev | grep Interface
# Should show 14+ wlan* entries

# If missing, check USB 3.0 connection and powered hub supply
lsusb | grep -i "Hak5\|MT7601"
```

## 3. Alfa AWUS036ACH

```bash
# Verify driver loaded
iw dev | grep -A2 wlan1
# Should show type: managed (will switch to monitor when daemon starts)

# Check chipset
lsusb -v 2>/dev/null | grep -A5 "Realtek\|Ralink"
```

## 4. H2T (Meshtastic node)

**One-time phone app setup (do this before connecting to Pi):**

1. Open Meshtastic app → connect to H2T via BLE
2. Radio config → set region to `US` (915MHz)
3. Set node name: `flockdar-hub`
4. Channels → Primary channel → name: `flockdar`, PSK: generate key, save key somewhere safe
5. Disconnect

```bash
# Verify H2T appears on Pi
ls /dev/ttyUSB* /dev/ttyACM*
# Note the device path (usually /dev/ttyUSB0)

# Verify meshtastic-go can connect (once daemon is installed)
flockdar mesh-info --port /dev/ttyUSB0
# Should show node info and GPS fix status
```

## 5. Install flockdard

```bash
# From your dev machine
task install TARGET=pi-pelican.local

# Or manually:
scp bin/flockdard-arm64 pi@pi-pelican.local:/usr/local/bin/flockdard
ssh pi@pi-pelican.local chmod +x /usr/local/bin/flockdard
```

## 6. Configure

```bash
ssh pi@pi-pelican.local

sudo mkdir -p /etc/flockdard
sudo tee /etc/flockdard/config.toml << 'EOF'
[node]
id   = "pi-pelican"
name = "Pelican Hub"

[modules]
enabled = ["coconut", "wifi", "ble", "meshtastic"]

[modules.coconut]
# auto-detected — no config needed

[modules.wifi]
interface = "wlan_alfa"   # set to actual Alfa interface name

[modules.ble]
adapter = "hci1"          # dedicated BT dongle; hci0 = Pi built-in

[modules.meshtastic]
port    = "/dev/ttyUSB0"
channel = "flockdar"
key     = "your-32-byte-hex-key-here"

[api]
listen = "0.0.0.0:8080"
tls    = false

[store]
path = "/var/lib/flockdard/detections.db"
EOF

sudo mkdir -p /var/lib/flockdard
```

## 7. Systemd service

```bash
# From your dev machine
task install:service TARGET=pi-pelican.local

# Or manually:
sudo tee /etc/systemd/system/flockdard.service << 'EOF'
[Unit]
Description=flockdard detection daemon
After=network.target bluetooth.target

[Service]
ExecStart=/usr/local/bin/flockdard --config /etc/flockdard/config.toml
Restart=on-failure
RestartSec=5
User=root
AmbientCapabilities=CAP_NET_RAW CAP_NET_ADMIN

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now flockdard
```

## 8. Verify

```bash
# Service running
systemctl status flockdard

# Web UI accessible
curl -s http://localhost:8080/api/v1/stats | jq .

# Modules loaded
curl -s http://localhost:8080/api/v1/stats | jq .modules

# From your phone/laptop browser
open http://pi-pelican.local:8080
```

## Field checklist

```
[ ] LiPo charged (12.6V = full, 10.5V = cutoff)
[ ] H2T has GPS fix — LED solid green, or check Meshtastic app
[ ] Coconut: iw dev | grep Interface returns 14+ entries
[ ] Daemon: systemctl status flockdard → active (running)
[ ] Web UI: http://pi-pelican.local:8080 loads from phone
[ ] Fans running (feel airflow through grilles)
[ ] All panel-mount connectors tight
```

## Troubleshooting

**Coconut not showing 14 interfaces:**
- Must be on USB 3.0 port (blue port on Pi 4)
- Powered hub must be on its own power rail, not through Pi
- `dmesg | grep usb` for driver errors

**H2T not appearing as /dev/ttyUSB0:**
- Check USB-C cable (data cable, not charge-only)
- `dmesg | tail -20` after plugging in
- May appear as `/dev/ttyACM0` on some kernels

**No GPS position in web UI:**
- H2T needs clear sky view or a few minutes indoors
- Check Meshtastic app → H2T → GPS status
- `flockdar mesh-info --port /dev/ttyUSB0` shows fix quality
