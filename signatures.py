"""
Flock Safety / Raven device signatures.

Sources:
  - NitekryDPaul promiscuous-mode WiFi OUI research
    https://github.com/DeflockJoplin/flock-you/blob/main/datasets/NitekryDPaul_wifi_ouis.md
  - flock-back signatures
    https://github.com/NSM-Barii/flock-back/blob/main/src/signatures.py
  - FlockSquawk DeviceSignatures.h
    https://github.com/f1yaw4y/FlockSquawk/blob/main/common/DeviceSignatures.h
  - flock-you confirmed device datasets (Flock-XXXXXX SSID, Penguin BLE, wildcard probe)
    https://github.com/DeflockJoplin/flock-you/tree/main/datasets
  - Field observation (flocknet SSID, eero backhaul, WiFi fingerprint)
"""

import re

# ---------------------------------------------------------------------------
# OUI prefixes (lowercase colon-separated, first 8 chars of MAC)
# ---------------------------------------------------------------------------

# High-confidence: directly registered to Flock Safety Inc.
FLOCK_DIRECT_OUIS = {
    "b4:1e:52",  # Flock Safety — direct IEEE registration (FlockSquawk)
}

# Chip-vendor OUIs observed on confirmed Flock hardware.
# Also appear on non-Flock devices — use with corroborating signals.
FLOCK_CHIP_OUIS = {
    # FS Ext Battery / Raven camera (Silicon Labs / Lite-On)
    "58:8e:81", "ec:1b:bd", "90:35:ea", "04:0d:84",
    "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
    "cc:cc:cc",
    # Flock WiFi devices (NitekryDPaul)
    "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "b8:35:32",
    "14:5a:fc", "74:4c:a1", "08:3a:88", "9c:2f:9d", "c0:35:32",
    "94:08:53", "e4:aa:ea", "f4:6a:dd", "f8:a2:d6", "24:b2:b9",
    "00:f4:8d", "d0:39:57", "e8:d0:fc", "e0:4f:43", "b8:1e:a4",
    "70:08:94", "3c:71:bf", "58:00:e3", "5c:93:a2", "64:6e:69",
    "48:27:ea", "a4:cf:12", "82:6b:f2",
}

ALL_OUIS = FLOCK_DIRECT_OUIS | FLOCK_CHIP_OUIS

# ---------------------------------------------------------------------------
# WiFi backhaul
# ---------------------------------------------------------------------------

# eero OUI used by Flock for camera-site mesh routers.
# High confidence when the SSID also contains "flock".
FLOCK_BACKHAUL_OUIS = {
    "80:da:13",  # eero Inc. — Flock's standard backhaul router
}

FLOCK_SSID_PATTERNS = [
    "flocknet",      # confirmed Flock internal mesh network name
    "flock",
    "fs ext battery",
    "penguin",
    "pigvision",
]

# Flock camera WiFi SSID: "Flock-XXXXXX" / "FLOCK-XXXXXX"
# XXXXXX == last 6 uppercase hex chars of the device MAC.
# Confirmed across 100s of field captures in the flock-you dataset.
FLOCK_CAMERA_SSID_RE = re.compile(r"^(Flock|FLOCK)-[0-9A-Fa-f]{6}$")

# Confirmed Flock camera WiFi fingerprint (from field dataset):
#   - No WPS, no WPA3/SAE, no 802.1X — plain WPA2-PSK
#   - Predominantly 2.4 GHz channels 1, 6, 11 (rarely 5 GHz)
# When a Flock chip-OUI device matches this capabilities string on one of
# these channels, it's a strong corroborating signal even with a hidden SSID.
FLOCK_WIFI_CAPAB = "[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]"
FLOCK_CAMERA_CHANNELS_MHZ = {2412, 2437, 2462}   # ch 1, 6, 11

# ---------------------------------------------------------------------------
# Wildcard probe request detection (passive 802.11, not applicable to WiGLE)
# ---------------------------------------------------------------------------
# Flock cameras send 802.11 wildcard probe requests (SSID tag length = 0)
# when waking briefly to upload. Detection rule: OUI in FLOCK_CHIP_OUIS
# AND frame is a Management/Probe-Request AND SSID tag length == 0.
# Implemented in flock-you main.cpp and FlockSquawk ThreatAnalyzer.
# WiGLE passive scans do not capture raw 802.11 frames, so this signal
# is not detectable from WiGLE exports — requires ESP32 promiscuous mode.

# ---------------------------------------------------------------------------
# BLE advertisement names
# ---------------------------------------------------------------------------

BLE_NAME_PATTERNS = [
    "fs ext battery",   # Flock Safety Extended Battery
    "flock",
    "pigvision",
]

# Penguin surveillance BLE devices use "Penguin-XXXXXXXXXX" (10-digit numeric).
# Source: flock-you Penguin dataset (mfgrid 2504, type BLE/BT, FL/TX deployments).
PENGUIN_BLE_SSID_RE = re.compile(r"^Penguin-\d{10}$")

# ---------------------------------------------------------------------------
# BLE manufacturer IDs (WiGLE mfgrid column)
# ---------------------------------------------------------------------------

# BLE company IDs that appear on confirmed Flock-ecosystem devices.
# The mfgrid value in WiGLE SQLite is the decimal BT company ID from the
# BLE advertisement manufacturer-specific data header.
FLOCK_MFGRIDS = {
    2504,   # Penguin surveillance devices (field-confirmed, flock-you dataset)
}

# ---------------------------------------------------------------------------
# Raven GATT service UUIDs
# ---------------------------------------------------------------------------

# Custom Flock/Raven services — NOT standard Bluetooth SIG UUIDs.
# Presence of any of these is high-confidence identification.
RAVEN_SERVICES_HIGH = {
    "00003100-0000-1000-8000-00805f9b34fb",  # GPS (fw 1.2.0+)
    "00003200-0000-1000-8000-00805f9b34fb",  # Power / Battery (fw 1.2.0+)
    "00003300-0000-1000-8000-00805f9b34fb",  # Network / LTE (fw 1.2.0+)
    "00003400-0000-1000-8000-00805f9b34fb",  # Upload statistics (fw 1.2.0+)
    "00003500-0000-1000-8000-00805f9b34fb",  # Error / Failure (fw 1.2.0+)
}

# Standard BT SIG UUIDs that Flock reused in firmware 1.1.7.
# Very common — only meaningful when also matched against OUI or name.
RAVEN_SERVICES_OLD = {
    "00001809-0000-1000-8000-00805f9b34fb",  # Health Thermometer (fw 1.1.7)
    "00001819-0000-1000-8000-00805f9b34fb",  # Location & Navigation (fw 1.1.7)
    "0000180a-0000-1000-8000-00805f9b34fb",  # Device Information (all versions)
}

# ---------------------------------------------------------------------------
# Raven GATT characteristics (for active interrogation)
# ---------------------------------------------------------------------------
# All characteristics live under the service UUIDs above.
# Reading these does not require authentication.

RAVEN_CHARACTERISTICS = {
    # Device Information (0x180a) — all firmware versions
    "00002a25-0000-1000-8000-00805f9b34fb": "Serial Number",
    "00002a24-0000-1000-8000-00805f9b34fb": "Model Number",
    "00002a26-0000-1000-8000-00805f9b34fb": "Firmware Version",
    "00003001-0000-1000-8000-00805f9b34fb": "Part Number",
    "00003002-0000-1000-8000-00805f9b34fb": "Serial Number (fw 1.2+)",
    "00003004-0000-1000-8000-00805f9b34fb": "MAC Address",
    # GPS (0x3100)
    "00003101-0000-1000-8000-00805f9b34fb": "GPS Latitude",
    "00003102-0000-1000-8000-00805f9b34fb": "GPS Longitude",
    "00003103-0000-1000-8000-00805f9b34fb": "GPS Altitude",
    # Power (0x3200)
    "00003201-0000-1000-8000-00805f9b34fb": "Board Temperature",
    "00003202-0000-1000-8000-00805f9b34fb": "Battery Voltage",
    "00003203-0000-1000-8000-00805f9b34fb": "Charge/Discharge Current",
    "00003204-0000-1000-8000-00805f9b34fb": "10W Solar Voltage",
    "00003205-0000-1000-8000-00805f9b34fb": "Battery State",
    # Network (0x3300)
    "00003301-0000-1000-8000-00805f9b34fb": "Last Connected",
    "00003302-0000-1000-8000-00805f9b34fb": "LTE Network Type",
    "00003303-0000-1000-8000-00805f9b34fb": "LTE Operator",
    "00003304-0000-1000-8000-00805f9b34fb": "LTE RSSI",
    "00003308-0000-1000-8000-00805f9b34fb": "Last Connected WiFi SSID",
    # Upload (0x3400)
    "00003401-0000-1000-8000-00805f9b34fb": "Average Upload Time",
    "00003402-0000-1000-8000-00805f9b34fb": "Most Recent Upload Time",
    "00003403-0000-1000-8000-00805f9b34fb": "Audio Uploads Since Boot",
    # Error (0x3500)
    "00003501-0000-1000-8000-00805f9b34fb": "Identity Check Failures",
    "00003502-0000-1000-8000-00805f9b34fb": "Status Update Failures",
    "00003503-0000-1000-8000-00805f9b34fb": "Heartbeat Failures",
    "00003504-0000-1000-8000-00805f9b34fb": "OTA Update Failures",
    "00003505-0000-1000-8000-00805f9b34fb": "Audio Upload Failures",
    # Old fw 1.1.7 (0x1809 / 0x1819)
    "00002a6e-0000-1000-8000-00805f9b34fb": "Temperature (old)",
    "00002a19-0000-1000-8000-00805f9b34fb": "Battery Level (old)",
    "00002aae-0000-1000-8000-00805f9b34fb": "Latitude (old)",
    "00002aaf-0000-1000-8000-00805f9b34fb": "Longitude (old)",
}

# ---------------------------------------------------------------------------
# Surveillance camera OUIs (non-Flock, for broader awareness)
# ---------------------------------------------------------------------------

SURVEILLANCE_OUIS = {
    "70:1a:d5": "Avigilon Alta",
    "00:40:8c": "Axis Communications",
    "ac:cc:8e": "Axis Communications",
    "b8:a4:4f": "Axis Communications",
    "e8:27:25": "Axis Communications",
    "00:13:56": "FLIR Systems",
    "00:40:7f": "FLIR Systems",
    "00:1b:d8": "FLIR Systems",
    "00:13:e2": "GeoVision",
    "44:b4:23": "Hanwha Vision",
    "8c:1d:55": "Hanwha Vision",
    "e4:30:22": "Hanwha Vision",
    "00:10:be": "March Networks",
    "00:12:81": "March Networks",
    "00:03:c5": "Mobotix",
    "00:1c:27": "Sunell Electronics",
}
