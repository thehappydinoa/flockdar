# Deploying flockdar on a Raspberry Pi

Run the ESP32 scanner + Meshtastic GPS as an always-on logger that writes a
SQLite you can open later in the TUI. Tested on Raspberry Pi OS (Bookworm,
64-bit) on a Pi 4 / Pi 5 / Zero 2 W.

## 1. Install flockdar

```bash
sudo apt update && sudo apt install -y pipx
pipx ensurepath
pipx install "flockdar[meshtastic]"      # includes the Meshtastic GPS extra
# new shell, then check:
flockdar-ingest --help
```

(Or with uv: `uv tool install "flockdar[meshtastic]"`.)

## 2. Serial permissions

```bash
sudo usermod -aG dialout "$USER"         # USB serial access; log out/in after
```

Identify the two USB devices (typically the ESP32 is `/dev/ttyUSB0` and the
Meshtastic node is `/dev/ttyACM0`):

```bash
ls -l /dev/serial/by-id/                 # stable names that survive reboots
```

Prefer the `by-id` paths in the service file — `ttyUSB0`/`ttyACM0` numbering can
swap between boots.

## 3. Quick test

```bash
flockdar-ingest /dev/ttyUSB0 ~/flock.sqlite --meshtastic /dev/ttyACM0
# Ctrl-C to stop, then review on any machine:
flockdar ~/flock.sqlite
```

You should see `Using Meshtastic node for GPS position.` and detection lines
with the Pi's live coordinates.

## 4. Run as a service

```bash
sudo cp deploy/flockdar-ingest.service /etc/systemd/system/
sudoedit /etc/systemd/system/flockdar-ingest.service   # set User + device paths
sudo systemctl daemon-reload
sudo systemctl enable --now flockdar-ingest
journalctl -u flockdar-ingest -f                        # watch detections
```

The service flushes `~/flock.sqlite` every 30 s and writes a final flush on
`systemctl stop`, so a power-cut loses at most the last interval.

## Notes

- **GPS over Wi-Fi/TCP instead of USB:** use `--meshtastic-host <ip>` if your
  node exposes the Meshtastic TCP API.
- **No ESP32 yet?** You can still develop against a saved log:
  `flockdar ~/some-capture.ndjson`.
- **HMAC key:** if you changed the firmware's `-DFD_HMAC_KEY`, pass the same
  value with `--key` or set `FLOCKDAR_HMAC_KEY` in the unit's `Environment=`.
