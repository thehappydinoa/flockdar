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
# Expanded from WiGLE /api/v2/stats/general manufacturerStatistics
# (May 2026) + IEEE OUI registry. Merged with SURVEILLANCE_OUIS at
# firmware generation time. Not used for Flock detection.

VENDOR_OUIS: dict[str, str] = {
    # --- 2Wire ---
    "00:14:95": "2Wire",
    "00:22:a4": "2Wire",
    "64:0f:28": "2Wire",
    "98:2c:be": "2Wire",
    "f8:2c:18": "2Wire",
    # --- ASUS ---
    "00:26:18": "ASUS",
    "04:92:26": "ASUS",
    "08:60:6e": "ASUS",
    "10:7b:44": "ASUS",
    "18:31:bf": "ASUS",
    "2c:4d:54": "ASUS",
    "30:5a:3a": "ASUS",
    "38:d5:47": "ASUS",
    "50:46:5d": "ASUS",
    "60:45:cb": "ASUS",
    "88:d7:f6": "ASUS",
    "ac:9e:17": "ASUS",
    "bc:ae:c5": "ASUS",
    # --- AVM ---
    "00:1f:3f": "AVM",
    "00:24:fe": "AVM",
    "2c:3a:fd": "AVM",
    "34:31:c4": "AVM",
    "98:9b:cb": "AVM",
    "bc:05:43": "AVM",
    "c0:25:06": "AVM",
    "cc:ce:1e": "AVM",
    "d0:12:cb": "AVM",
    "d4:24:dd": "AVM",
    # --- Actiontec ---
    "00:18:01": "Actiontec",
    "18:1b:eb": "Actiontec",
    "20:76:00": "Actiontec",
    "a0:a3:e2": "Actiontec",
    "fc:2b:b2": "Actiontec",
    # --- Amazon ---
    "34:d2:70": "Amazon",
    "48:93:8d": "Amazon",
    "50:dc:e7": "Amazon",
    "68:37:e9": "Amazon",
    "74:c2:46": "Amazon",
    "84:d6:d0": "Amazon",
    "f0:27:2d": "Amazon",
    # --- Apple ---
    "00:03:93": "Apple",
    "00:0a:95": "Apple",
    "00:10:fa": "Apple",
    "00:11:24": "Apple",
    "00:14:51": "Apple",
    "00:16:cb": "Apple",
    "00:17:f2": "Apple",
    "00:19:e3": "Apple",
    "00:1b:63": "Apple",
    "00:1d:4f": "Apple",
    "00:1e:52": "Apple",
    "00:1e:c2": "Apple",
    "00:1f:5b": "Apple",
    "00:1f:f3": "Apple",
    "00:21:e9": "Apple",
    "00:22:41": "Apple",
    "00:23:12": "Apple",
    "00:23:32": "Apple",
    "00:23:6c": "Apple",
    "00:23:df": "Apple",
    "00:24:36": "Apple",
    "00:25:00": "Apple",
    "00:25:4b": "Apple",
    "00:25:bc": "Apple",
    "00:26:08": "Apple",
    "00:26:4a": "Apple",
    "00:26:b0": "Apple",
    "00:26:bb": "Apple",
    "00:30:65": "Apple",
    "00:3e:e1": "Apple",
    "00:50:e4": "Apple",
    "00:56:cd": "Apple",
    "00:61:71": "Apple",
    "00:6d:52": "Apple",
    "00:7d:60": "Apple",
    "00:88:65": "Apple",
    "04:52:f3": "Apple",
    "28:cf:e9": "Apple",
    "34:8c:5e": "Apple",
    "3c:06:30": "Apple",
    "40:33:1a": "Apple",
    "58:ad:12": "Apple",
    "5c:95:ae": "Apple",
    "60:fd:a6": "Apple",
    "64:a5:c3": "Apple",
    "78:4f:43": "Apple",
    "7c:d1:c3": "Apple",
    "80:a9:97": "Apple",
    "88:66:5a": "Apple",
    "8c:7c:92": "Apple",
    "a4:83:e7": "Apple",
    "ac:de:48": "Apple",
    "bc:4c:c4": "Apple",
    "d0:03:4b": "Apple",
    "f0:18:98": "Apple",
    "f0:ee:7a": "Apple",
    "f4:5c:89": "Apple",
    # --- Arcadyan ---
    "04:09:86": "Arcadyan",
    "18:a5:ff": "Arcadyan",
    "84:90:0a": "Arcadyan",
    "8c:19:b5": "Arcadyan",
    "a8:a2:37": "Arcadyan",
    # --- Arris ---
    "00:00:ca": "Arris",
    "00:15:cf": "Arris",
    "00:1a:2b": "Arris",
    "00:1d:cd": "Arris",
    "00:1d:d4": "Arris",
    "00:23:75": "Arris",
    "00:24:95": "Arris",
    "00:25:f1": "Arris",
    "20:3d:66": "Arris",
    "5c:8f:e0": "Arris",
    "6c:2e:33": "Arris",
    # --- Aruba ---
    "00:0b:86": "Aruba",
    "00:24:6c": "Aruba",
    "24:de:c6": "Aruba",
    "6c:f3:7f": "Aruba",
    "9c:1c:12": "Aruba",
    # --- Askey ---
    "00:21:63": "Askey",
    "08:b0:55": "Askey",
    "50:5f:b5": "Askey",
    "88:de:7c": "Askey",
    "a0:64:8f": "Askey",
    # --- Avigilon ---
    "70:1a:d5": "Avigilon",
    # --- Axis ---
    "00:40:8c": "Axis",
    "ac:cc:8e": "Axis",
    "b8:a4:4f": "Axis",
    "e8:27:25": "Axis",
    # --- Belkin ---
    "08:86:3b": "Belkin",
    "58:ef:68": "Belkin",
    "60:38:e0": "Belkin",
    "94:10:3e": "Belkin",
    "94:44:52": "Belkin",
    "d8:ec:5e": "Belkin",
    "e8:9f:80": "Belkin",
    # --- Bosch ---
    "00:04:63": "Bosch",
    "00:07:5f": "Bosch",
    # --- Buffalo ---
    "00:07:40": "Buffalo",
    "00:24:a5": "Buffalo",
    "34:3d:c4": "Buffalo",
    "cc:e1:d5": "Buffalo",
    "d4:2c:46": "Buffalo",
    # --- Cisco ---
    "00:06:c1": "Cisco",
    "00:0b:be": "Cisco",
    "00:0c:41": "Cisco",
    "00:0d:65": "Cisco",
    "00:0e:38": "Cisco",
    "00:10:07": "Cisco",
    "00:11:20": "Cisco",
    "00:12:00": "Cisco",
    "00:13:19": "Cisco",
    "00:14:1b": "Cisco",
    "00:15:62": "Cisco",
    "00:16:46": "Cisco",
    "00:17:0e": "Cisco",
    "00:17:94": "Cisco",
    "00:18:18": "Cisco",
    "00:19:07": "Cisco",
    "00:1a:2f": "Cisco",
    "00:1a:a1": "Cisco",
    "00:1b:0d": "Cisco",
    "00:1b:2a": "Cisco",
    "00:1c:0e": "Cisco",
    "00:1d:45": "Cisco",
    "00:1e:13": "Cisco",
    "00:1f:26": "Cisco",
    "00:21:1b": "Cisco",
    "00:22:0c": "Cisco",
    "00:23:04": "Cisco",
    "00:24:14": "Cisco",
    "00:25:2e": "Cisco",
    "00:26:0a": "Cisco",
    "00:30:19": "Cisco",
    "00:50:0b": "Cisco",
    "00:90:0c": "Cisco",
    "00:d0:06": "Cisco",
    "00:e0:14": "Cisco",
    "48:1b:a4": "Cisco",
    "68:71:61": "Cisco",
    "6c:03:b5": "Cisco",
    "70:81:85": "Cisco",
    "90:88:55": "Cisco",
    "e8:0a:b9": "Cisco",
    # --- Compal ---
    "34:2c:c4": "Compal",
    "38:43:7d": "Compal",
    "54:67:51": "Compal",
    "b4:f2:67": "Compal",
    "dc:53:7c": "Compal",
    # --- D-Link ---
    "00:05:5d": "D-Link",
    "00:0d:88": "D-Link",
    "00:11:95": "D-Link",
    "00:17:9a": "D-Link",
    "00:1c:f0": "D-Link",
    "00:1e:58": "D-Link",
    "00:22:b0": "D-Link",
    "00:50:ba": "D-Link",
    "10:62:eb": "D-Link",
    "1c:7e:e5": "D-Link",
    "28:10:7b": "D-Link",
    "74:da:da": "D-Link",
    "a0:a3:f0": "D-Link",
    "bc:0f:9a": "D-Link",
    "bc:22:28": "D-Link",
    # --- Dahua ---
    "08:ed:ed": "Dahua",
    "24:48:45": "Dahua",
    "3c:ef:8c": "Dahua",
    "4c:11:bf": "Dahua",
    "90:02:a9": "Dahua",
    # --- Ecobee ---
    "44:61:32": "Ecobee",
    "dc:ee:06": "Ecobee",
    # --- Extreme ---
    "08:ea:44": "Extreme",
    "a8:c6:47": "Extreme",
    "b8:7c:f2": "Extreme",
    "e0:a1:29": "Extreme",
    "f4:ea:b5": "Extreme",
    # --- FLIR ---
    "00:13:56": "FLIR",
    "00:1b:d8": "FLIR",
    "00:40:7f": "FLIR",
    # --- Fortinet ---
    "00:09:0f": "Fortinet",
    "08:5b:0e": "Fortinet",
    # --- Freebox ---
    "00:07:cb": "Freebox",
    "14:0c:76": "Freebox",
    "38:07:16": "Freebox",
    "8c:97:ea": "Freebox",
    "dc:00:b0": "Freebox",
    # --- GeoVision ---
    "00:13:e2": "GeoVision",
    # --- Google ---
    "00:1a:11": "Google",
    "08:b4:b1": "Google",
    "24:29:34": "Google",
    "54:60:09": "Google",
    "60:70:6c": "Google",
    "60:b7:6e": "Google",
    "64:16:66": "Google",
    "6c:0b:84": "Google",
    "94:eb:2c": "Google",
    "c8:2a:dd": "Google",
    "da:a1:19": "Google",
    "f4:f5:e8": "Google",
    # --- HP ---
    "10:e7:c6": "HP",
    "40:b0:34": "HP",
    "80:ce:62": "HP",
    "9c:7b:ef": "HP",
    "b8:af:67": "HP",
    # --- HPE ---
    "14:02:ec": "HPE",
    "1c:98:ec": "HPE",
    "24:f2:7f": "HPE",
    "80:8d:b7": "HPE",
    "94:18:82": "HPE",
    # --- Hanwha ---
    "44:b4:23": "Hanwha",
    "8c:1d:55": "Hanwha",
    "e4:30:22": "Hanwha",
    # --- Hikvision ---
    "28:57:be": "Hikvision",
    "44:19:b6": "Hikvision",
    "54:c4:15": "Hikvision",
    "88:44:77": "Hikvision",
    "bc:ad:28": "Hikvision",
    "c0:56:e3": "Hikvision",
    # --- Hitron ---
    "00:07:d8": "Hitron",
    "68:b6:fc": "Hitron",
    "74:9b:e8": "Hitron",
    "b0:f5:30": "Hitron",
    "f8:1d:0f": "Hitron",
    # --- Honeywell ---
    "00:19:88": "Honeywell",
    "48:df:37": "Honeywell",
    # --- Huawei ---
    "00:18:82": "Huawei",
    "00:25:9e": "Huawei",
    "00:46:4b": "Huawei",
    "28:6e:d4": "Huawei",
    "54:43:d2": "Huawei",
    "54:44:3b": "Huawei",
    "5c:70:75": "Huawei",
    "78:2d:ad": "Huawei",
    "d8:da:f1": "Huawei",
    "e0:06:30": "Huawei",
    # --- Intel ---
    "00:1b:21": "Intel",
    "34:13:e8": "Intel",
    # --- Linksys ---
    "00:04:5a": "Linksys",
    "00:06:25": "Linksys",
    "00:0f:66": "Linksys",
    "00:12:17": "Linksys",
    "00:14:bf": "Linksys",
    "00:18:39": "Linksys",
    "00:1a:70": "Linksys",
    "00:1c:10": "Linksys",
    "00:1d:7e": "Linksys",
    "00:21:29": "Linksys",
    "00:23:69": "Linksys",
    "00:25:9c": "Linksys",
    "48:f8:b3": "Linksys",
    "58:6d:8f": "Linksys",
    "c0:c1:c0": "Linksys",
    "c4:41:1e": "Linksys",
    "c8:d7:19": "Linksys",
    # --- March ---
    "00:10:be": "March",
    "00:12:81": "March",
    # --- Meraki ---
    "0c:8d:db": "Meraki",
    "88:28:9d": "Meraki",
    # --- Meta ---
    "90:48:46": "Meta",
    "f4:17:b8": "Meta",
    # --- Microsoft ---
    "00:15:5d": "Microsoft",
    "7c:ed:8d": "Microsoft",
    # --- MikroTik ---
    "00:10:95": "MikroTik",
    "4c:5e:0c": "MikroTik",
    "6c:3b:6b": "MikroTik",
    "d4:ca:6d": "MikroTik",
    # --- Mitsumi ---
    "00:a0:96": "Mitsumi",
    "78:61:7c": "Mitsumi",
    "c4:49:bb": "Mitsumi",
    "f0:ab:54": "Mitsumi",
    "f8:3c:80": "Mitsumi",
    # --- Mobotix ---
    "00:03:c5": "Mobotix",
    # --- Motorola ---
    "00:04:20": "Motorola",
    "00:13:a9": "Motorola",
    "00:19:b9": "Motorola",
    "00:22:cf": "Motorola",
    # --- Netgear ---
    "00:09:5b": "Netgear",
    "00:0f:b0": "Netgear",
    "00:14:6c": "Netgear",
    "00:18:4d": "Netgear",
    "00:1b:2f": "Netgear",
    "00:1f:33": "Netgear",
    "00:24:b2": "Netgear",
    "00:26:f2": "Netgear",
    "10:0c:6b": "Netgear",
    "10:0d:7f": "Netgear",
    "20:0c:c8": "Netgear",
    "2c:b0:5d": "Netgear",
    "38:94:ed": "Netgear",
    "40:5d:82": "Netgear",
    "84:1b:5e": "Netgear",
    "9c:c9:eb": "Netgear",
    "a0:04:60": "Netgear",
    "c0:3f:0e": "Netgear",
    "dc:ef:09": "Netgear",
    # --- Pegatron ---
    "0c:54:a5": "Pegatron",
    "20:25:64": "Pegatron",
    "54:be:f7": "Pegatron",
    "7c:05:07": "Pegatron",
    "d4:5d:df": "Pegatron",
    # --- Pelco ---
    "00:06:f6": "Pelco",
    # --- Philips Hue ---
    "00:17:88": "Philips Hue",
    # --- Reolink ---
    "94:71:ac": "Reolink",
    "ec:71:db": "Reolink",
    # --- Ring ---
    "34:e1:2d": "Ring",
    "9c:ef:d5": "Ring",
    # --- Roku ---
    "08:05:81": "Roku",
    "12:59:32": "Roku",
    "40:ca:63": "Roku",
    "88:de:a9": "Roku",
    "8e:49:62": "Roku",
    "b0:a7:37": "Roku",
    "cc:6d:a0": "Roku",
    # --- Ruckus ---
    "00:22:7f": "Ruckus",
    "24:c9:a1": "Ruckus",
    "34:fa:9f": "Ruckus",
    "38:45:3b": "Ruckus",
    "5c:df:89": "Ruckus",
    "60:73:5c": "Ruckus",
    "60:d0:2c": "Ruckus",
    "84:95:37": "Ruckus",
    "d4:bd:4f": "Ruckus",
    # --- Sagemcom ---
    "04:e3:1a": "Sagemcom",
    "58:1d:d8": "Sagemcom",
    "ac:d7:5b": "Sagemcom",
    "b0:5b:99": "Sagemcom",
    "cc:00:f1": "Sagemcom",
    # --- Samsung ---
    "00:07:ab": "Samsung",
    "00:12:47": "Samsung",
    "00:12:fb": "Samsung",
    "00:15:6d": "Samsung",
    "00:15:b9": "Samsung",
    "00:16:6b": "Samsung",
    "00:16:db": "Samsung",
    "00:1e:7d": "Samsung",
    "00:23:39": "Samsung",
    "00:26:37": "Samsung",
    "38:8a:06": "Samsung",
    "48:bc:e1": "Samsung",
    "5c:0a:5b": "Samsung",
    "64:1b:2f": "Samsung",
    "8c:77:12": "Samsung",
    "9c:73:b1": "Samsung",
    "bc:44:86": "Samsung",
    "c0:bd:d1": "Samsung",
    "d0:22:be": "Samsung",
    "d0:d0:03": "Samsung",
    "e4:7c:f9": "Samsung",
    # --- Schlage ---
    "00:1a:75": "Schlage",
    # --- Sercomm ---
    "10:50:72": "Sercomm",
    "38:02:de": "Sercomm",
    "74:9d:79": "Sercomm",
    "d4:60:e3": "Sercomm",
    "e8:1b:69": "Sercomm",
    # --- SimpliSafe ---
    "44:73:68": "SimpliSafe",
    # --- Sky ---
    "00:1a:1e": "Sky",
    "00:1d:df": "Sky",
    # --- Sonos ---
    "00:0e:58": "Sonos",
    "5c:aa:fd": "Sonos",
    "78:28:ca": "Sonos",
    "94:9f:3e": "Sonos",
    # --- Sunell ---
    "00:1c:27": "Sunell",
    # --- Synology ---
    "00:11:32": "Synology",
    # --- TP-Link ---
    "00:31:92": "TP-Link",
    "14:d8:64": "TP-Link",
    "14:eb:b6": "TP-Link",
    "1c:3b:f3": "TP-Link",
    "24:69:68": "TP-Link",
    "50:c7:bf": "TP-Link",
    "54:af:97": "TP-Link",
    "60:e3:27": "TP-Link",
    "68:dd:b7": "TP-Link",
    "6c:b1:58": "TP-Link",
    "98:da:c4": "TP-Link",
    "ac:84:c6": "TP-Link",
    "b0:95:75": "TP-Link",
    "c0:25:e9": "TP-Link",
    "ec:08:6b": "TP-Link",
    # --- Technicolor ---
    "00:14:7f": "Technicolor",
    "1c:e4:dd": "Technicolor",
    "2c:30:1a": "Technicolor",
    "38:f1:8f": "Technicolor",
    "44:32:c8": "Technicolor",
    "f0:16:28": "Technicolor",
    "f8:fe:a8": "Technicolor",
    # --- Ubiquiti ---
    "00:27:22": "Ubiquiti",
    "24:5a:4c": "Ubiquiti",
    "44:d9:e7": "Ubiquiti",
    "68:d7:9a": "Ubiquiti",
    "74:83:c2": "Ubiquiti",
    "74:ac:b9": "Ubiquiti",
    "78:45:58": "Ubiquiti",
    "78:8a:20": "Ubiquiti",
    "80:2a:a8": "Ubiquiti",
    "b4:fb:e4": "Ubiquiti",
    "dc:9f:db": "Ubiquiti",
    "e0:63:da": "Ubiquiti",
    "f0:9f:c2": "Ubiquiti",
    "fc:ec:da": "Ubiquiti",
    # --- Uniview ---
    "24:0f:9b": "Uniview",
    "48:ea:63": "Uniview",
    # --- Verizon ---
    "00:19:15": "Verizon",
    "00:20:e0": "Verizon",
    "00:24:7b": "Verizon",
    # --- Vivotek ---
    "00:02:d1": "Vivotek",
    # --- Wyze ---
    "2c:aa:8e": "Wyze",
    "44:00:49": "Wyze",
    "d8:43:ae": "Wyze",
    # --- ZTE ---
    "90:c7:10": "ZTE",
    "98:ee:8c": "ZTE",
    "dc:51:93": "ZTE",
    "f0:1b:24": "ZTE",
    "f4:2e:48": "ZTE",
    # --- Zyxel ---
    "00:19:cb": "Zyxel",
    "00:23:f8": "Zyxel",
    "1c:74:0d": "Zyxel",
    "88:ac:c0": "Zyxel",
    "f8:0d:a9": "Zyxel",
    # --- eero ---
    "08:f0:1e": "eero",
    "48:b4:24": "eero",
    "7c:7e:f9": "eero",
    "80:da:13": "eero",
    "98:ed:7e": "eero",
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
    29: "Qualcomm",
    631: "Xiaomi",
    1178: "Nordic",
    100: "HP",
    48: "NXP",
}
