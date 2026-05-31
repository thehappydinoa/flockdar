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

# ---------------------------------------------------------------------------
# US-market vendor OUIs for nearby-device labeling (ESP32 UI / wardrive).
# Merged with SURVEILLANCE_OUIS at firmware generation time. Not used for
# Flock detection — see detect.py SURVEILLANCE_OUI signal for that.
# ---------------------------------------------------------------------------

VENDOR_OUIS: dict[str, str] = {
    # --- Phones / consumer mobile ---
    "00:03:93": "Apple",
    "04:52:f3": "Apple",
    "28:cf:e9": "Apple",
    "3c:06:30": "Apple",
    "40:33:1a": "Apple",
    "5c:95:ae": "Apple",
    "64:a5:c3": "Apple",
    "78:4f:43": "Apple",
    "7c:d1:c3": "Apple",
    "88:66:5a": "Apple",
    "8c:7c:92": "Apple",
    "a4:83:e7": "Apple",
    "ac:de:48": "Apple",
    "bc:4c:c4": "Apple",
    "d0:03:4b": "Apple",
    "f0:18:98": "Apple",
    "f4:5c:89": "Apple",
    "00:1a:11": "Google",
    "54:60:09": "Google",
    "64:16:66": "Google",
    "6c:0b:84": "Google",
    "94:eb:2c": "Google",
    "da:a1:19": "Google",
    "f4:f5:e8": "Google",
    "00:12:fb": "Samsung",
    "00:15:b9": "Samsung",
    "00:16:6b": "Samsung",
    "5c:0a:5b": "Samsung",
    "8c:77:12": "Samsung",
    "bc:44:86": "Samsung",
    "c0:bd:d1": "Samsung",
    "d0:22:be": "Samsung",
    "e4:7c:f9": "Samsung",
    "00:15:5d": "Microsoft",
    "7c:ed:8d": "Microsoft",
    "34:d2:70": "Amazon",
    "48:93:8d": "Amazon",
    "50:dc:e7": "Amazon",
    "68:37:e9": "Amazon",
    "74:c2:46": "Amazon",
    "84:d6:d0": "Amazon",
    "f0:27:2d": "Amazon",
    "90:48:46": "Meta",
    "f4:17:b8": "Meta",
    # --- WiFi routers / mesh / ISP CPE (US) ---
    "80:da:13": "eero",
    "88:28:9d": "Meraki",
    "0c:8d:db": "Meraki",
    "00:0b:be": "Cisco",
    "00:17:94": "Cisco",
    "00:1a:a1": "Cisco",
    "00:1b:0d": "Cisco",
    "70:81:85": "Cisco",
    "00:18:39": "Linksys",
    "00:1a:70": "Linksys",
    "00:1c:10": "Linksys",
    "58:6d:8f": "Linksys",
    "c4:41:1e": "Linksys",
    "20:0c:c8": "Netgear",
    "84:1b:5e": "Netgear",
    "a0:04:60": "Netgear",
    "c0:3f:0e": "Netgear",
    "10:0d:7f": "Netgear",
    "2c:b0:5d": "Netgear",
    "14:eb:b6": "TP-Link",
    "50:c7:bf": "TP-Link",
    "60:e3:27": "TP-Link",
    "98:da:c4": "TP-Link",
    "b0:95:75": "TP-Link",
    "c0:25:e9": "TP-Link",
    "ec:08:6b": "TP-Link",
    "54:af:97": "TP-Link",
    "24:5a:4c": "Ubiquiti",
    "44:d9:e7": "Ubiquiti",
    "68:d7:9a": "Ubiquiti",
    "74:ac:b9": "Ubiquiti",
    "78:45:58": "Ubiquiti",
    "80:2a:a8": "Ubiquiti",
    "b4:fb:e4": "Ubiquiti",
    "dc:9f:db": "Ubiquiti",
    "f0:9f:c2": "Ubiquiti",
    "fc:ec:da": "Ubiquiti",
    "08:60:6e": "ASUS",
    "10:7b:44": "ASUS",
    "2c:4d:54": "ASUS",
    "30:5a:3a": "ASUS",
    "38:d5:47": "ASUS",
    "50:46:5d": "ASUS",
    "ac:9e:17": "ASUS",
    "bc:ae:c5": "ASUS",
    "00:00:ca": "Arris",
    "00:1d:cd": "Arris",
    "00:1e:c2": "Arris",
    "00:25:f1": "Arris",
    "20:3d:66": "Arris",
    "5c:8f:e0": "Arris",
    "6c:2e:33": "Arris",
    "00:04:20": "Motorola",
    "00:13:a9": "Motorola",
    "00:19:b9": "Motorola",
    "00:22:cf": "Motorola",
    "00:19:15": "Verizon",
    "00:20:e0": "Verizon",
    "00:24:7b": "Verizon",
    "00:14:7f": "Technicolor",
    "00:19:cb": "Technicolor",
    "44:32:c8": "Technicolor",
    "00:05:5d": "D-Link",
    "00:0d:88": "D-Link",
    "00:11:95": "D-Link",
    "1c:7e:e5": "D-Link",
    "28:10:7b": "D-Link",
    "08:86:3b": "Belkin",
    "94:10:3e": "Belkin",
    "00:11:32": "Synology",
    "00:22:7f": "Ruckus",
    "24:c9:a1": "Ruckus",
    "60:73:5c": "Ruckus",
    "84:95:37": "Ruckus",
    "00:0b:86": "Aruba",
    "24:de:c6": "Aruba",
    "6c:f3:7f": "Aruba",
    "9c:1c:12": "Aruba",
    "00:09:0f": "Fortinet",
    "08:5b:0e": "Fortinet",
    "00:1b:21": "Intel",
    "34:13:e8": "Intel",
    "00:0e:58": "Sonos",
    "5c:aa:fd": "Sonos",
    "78:28:ca": "Sonos",
    "94:9f:3e": "Sonos",
    "b0:a7:37": "Roku",
    "08:05:81": "Roku",
    "88:de:a9": "Roku",
    "cc:6d:a0": "Roku",
    "00:17:88": "Philips Hue",
    "44:61:32": "Ecobee",
    "dc:ee:06": "Ecobee",
    # --- Security / smart home (US) ---
    "9c:ef:d5": "Ring",
    "34:e1:2d": "Ring",
    "44:73:68": "SimpliSafe",
    "00:1a:75": "Schlage",
    # --- IP / consumer cameras (US market) ---
    "44:19:b6": "Hikvision",
    "bc:ad:28": "Hikvision",
    "28:57:be": "Hikvision",
    "54:c4:15": "Hikvision",
    "c0:56:e3": "Hikvision",
    "88:44:77": "Hikvision",
    "3c:ef:8c": "Dahua",
    "24:48:45": "Dahua",
    "90:02:a9": "Dahua",
    "4c:11:bf": "Dahua",
    "08:ed:ed": "Dahua",
    "ec:71:db": "Reolink",
    "94:71:ac": "Reolink",
    "2c:aa:8e": "Wyze",
    "d8:43:ae": "Wyze",
    "44:00:49": "Wyze",
    "00:40:8c": "Axis",
    "ac:cc:8e": "Axis",
    "b8:a4:4f": "Axis",
    "e8:27:25": "Axis",
    "70:1a:d5": "Avigilon",
    "00:13:56": "FLIR",
    "00:40:7f": "FLIR",
    "00:1b:d8": "FLIR",
    "44:b4:23": "Hanwha",
    "8c:1d:55": "Hanwha",
    "e4:30:22": "Hanwha",
    "00:13:e2": "GeoVision",
    "00:10:be": "March",
    "00:12:81": "March",
    "00:03:c5": "Mobotix",
    "00:1c:27": "Sunell",
    "00:04:63": "Bosch",
    "00:07:5f": "Bosch",
    "00:19:88": "Honeywell",
    "48:df:37": "Honeywell",
    "00:06:f6": "Pelco",
    "00:02:d1": "Vivotek",
    "24:0f:9b": "Uniview",
    "48:ea:63": "Uniview",
}

# Bluetooth SIG company IDs for nearby BLE labeling (decimal).
BLE_VENDOR_IDS: dict[int, str] = {
    6: "Microsoft",
    13: "Plantronics",
    17: "Motorola",
    76: "Apple",
    89: "Texas Instruments",
    115: "Honeywell",
    117: "Samsung",
    128: "Silicon Labs",
    135: "Garmin",
    156: "Fitbit",
    158: "Bose",
    164: "Sonos",
    207: "Beats",
    224: "Google",
    242: "Nest",
    301: "Whoop",
    348: "Meta",
    393: "Amazon",
    465: "August",
    741: "Espressif",
    737: "Tile",
    911: "Broadcom",
    2504: "Flock",
}
