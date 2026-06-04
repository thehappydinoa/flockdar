# ADR-0014: Android Bluetooth tethering for Pi internet access

**Status:** Accepted  
**Date:** 2026-06-04

## Context

In field deployments the Pelican Hub Pi may have no Ethernet or WiFi connection to the internet. Signature updates, NTP sync, and optional C2 connectivity require outbound internet access. The operator's Android phone (already present for the Meshtastic app and web UI) can share its LTE/5G connection to the Pi via Bluetooth PAN tethering — no WiFi required, leaving the Pi's WiFi interfaces free for scanning.

## Decision

Support Android Bluetooth PAN tethering as the Pi's internet backhaul in field deployments. Document setup. The daemon detects the `bnep0` interface and routes outbound traffic through it when present.

## How it works

Android's "Bluetooth tethering" implements Bluetooth PAN (Personal Area Network) using the BNEP protocol. On Linux, BlueZ creates a `bnep0` network interface when paired and connected to an Android tethering host. The interface behaves like a standard Ethernet interface — DHCP, routing, DNS all work normally.

```mermaid
sequenceDiagram
    participant AND as Android phone\n(LTE/5G)
    participant PI as Raspberry Pi
    participant NET as Internet

    AND->>PI: Bluetooth PAN (BNEP)\npaired + connected
    Note over PI: bnep0 interface appears\nDHCP lease from Android
    PI->>AND: outbound traffic via bnep0
    AND->>NET: NAT via LTE/5G
    NET->>AND: response
    AND->>PI: response via bnep0

    Note over PI: wlan0/wlan1 stay dedicated\nto WiFi scanning — never used for internet
```

## Why Bluetooth over WiFi tethering

- **WiFi interfaces are scanning radios** — using wlan0 as a hotspot client conflicts with monitor mode and degrades scan coverage
- **Bluetooth is a separate radio** — Pi 4B has Bluetooth 5.0 built in; tethering uses it without touching WiFi
- **Range** — adequate for a car or building deployment (10m+)
- **Battery** — BT tethering drains Android battery ~2× slower than WiFi hotspot

## Linux setup (Pi side)

```bash
# Install bluetooth tools (if not present)
sudo apt install bluez bluez-tools

# Pair with Android phone (one-time)
bluetoothctl
  power on
  agent on
  scan on
  # find phone MAC, e.g. AA:BB:CC:DD:EE:FF
  pair AA:BB:CC:DD:EE:FF
  trust AA:BB:CC:DD:EE:FF
  quit

# Connect PAN (run after pairing, or automate via systemd)
bt-network -c AA:BB:CC:DD:EE:FF nap

# bnep0 should appear; get DHCP lease
sudo dhclient bnep0

# Verify
ip route show   # should show default via bnep0 or alongside existing routes
curl -s https://ifconfig.me  # should return Android's LTE IP
```

## Android setup

Settings → Connections → Mobile Hotspot and Tethering → Bluetooth tethering → ON

The phone must be paired with the Pi first (standard Bluetooth pairing from Android side).

## Automation via systemd

```ini
# /etc/systemd/system/bt-tether@.service
[Unit]
Description=Bluetooth PAN tether to %i
After=bluetooth.service
Requires=bluetooth.service

[Service]
ExecStartPre=/usr/bin/bluetoothctl connect %i
ExecStart=/usr/bin/bt-network -c %i nap
ExecStartPost=/usr/bin/dhclient bnep0
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

```bash
# Enable for a specific phone MAC
sudo systemctl enable --now "bt-tether@AA:BB:CC:DD:EE:FF"
```

## Daemon awareness

The daemon does not need to manage the tether directly. When `bnep0` is up, outbound connections (signature updates, NTP) route through it automatically. The daemon's startup sequence:

1. Start scanning modules immediately (no internet required)
2. In background: check internet connectivity, attempt signature update if reachable
3. Log warning if no internet — never block scanning

`task oui:update` and `task sigs:pull` (future) run over whatever interface is available, including `bnep0`.

## Limitations

- **Throughput**: BT PAN typically 1–3 Mbps — adequate for signature updates and NTP, not for streaming PCAP to a remote host
- **Latency**: higher than WiFi/Ethernet — fine for background tasks
- **Android screen**: Bluetooth tethering keeps the phone's Bluetooth active; phone does not need to stay unlocked
- **One connection**: Pi pairs to one phone at a time; if the operator changes phones, re-pair required

## Consequences

- BT tethering documented in `docs/setup/pelican-hub.md`
- `systemd/bt-tether@.service` unit file added to `deploy/`
- `Taskfile.yml` gains `bt:pair` and `bt:connect` tasks for convenience
- No daemon code changes required — OS routing handles it transparently
- WiFi interfaces remain exclusively dedicated to scanning
