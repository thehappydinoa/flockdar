# Architecture

## System overview

```mermaid
graph TB
    subgraph Field["Field (mobile)"]
        TD[T-Deck\nESP32-S3\nflockdar firmware]
    end

    subgraph Hub["Pelican Case (stationary)"]
        H2T[H2T\nstock Meshtastic\nLoRa + GPS]
        PI[Raspberry Pi 4B\nmuninnd daemon]
        COC[WiFi Coconut\n14× 2.4GHz]
        ALFA[Alfa AWUS036ACH\n2.4 + 5GHz]
        BLE[USB BT adapter\nBLE]

        H2T -- USB serial\nmeshtastic-go --> PI
        COC -- USB 3.0 --> PI
        ALFA -- USB --> PI
        BLE -- USB --> PI
    end

    subgraph Clients["Clients"]
        BROWSER[Browser / Phone\nweb UI]
        ANDROID[Android app\nBLE scanner]
    end

    TD -- LoRa 915MHz\nMeshtastic PRIVATE_APP --> H2T
    H2T -- LoRa 915MHz\ndisplay updates --> TD
    PI -- WebSocket\nlive hit feed --> BROWSER
    PI -- REST API --> ANDROID
    ANDROID -- sync POST --> PI
```

## Data flow: detection to display

```mermaid
flowchart LR
    subgraph Scanners
        W[WiFi Coconut\ngoroutine × 14]
        A[Alfa monitor\ngoroutine]
        B[BLE scanner\ngoroutine]
        S[Serial ESP32\ngoroutine]
        M[Meshtastic\ngoroutine]
    end

    subgraph Daemon core
        CH[(detection\nchannel)]
        DET[detect.Analyze]
        STORE[(SQLite)]
        WS[WebSocket\nhub]
        GPS[GPS position\nmanager]
    end

    W & A & B & S & M --> CH
    CH --> DET
    DET -->|Hit| STORE
    DET -->|Hit| WS
    H2T_GPS[H2T position\npackets] --> GPS
    GPS -->|geotag| DET
    WS -->|push| BROWSER[Browser]
```

## T-Deck LoRa communication

```mermaid
sequenceDiagram
    participant TD as T-Deck\n(ESP32 firmware)
    participant H2T as H2T\n(stock Meshtastic)
    participant GW as meshtastic-go\n(Pi daemon module)
    participant D as Daemon core

    Note over TD: WiFi radio in monitor mode — never interrupted

    TD->>H2T: LoRa: MeshPacket(PRIVATE_APP)\nFlockdarHit{mac, rssi, gps, run_id} 28 bytes
    H2T->>GW: USB serial: decoded packet bytes
    GW->>D: Hit{via:lora, node:"t-deck-van", ...}
    D->>D: store + geotag + broadcast to WebSocket

    D->>GW: send display update
    GW->>H2T: USB serial: MeshPacket to t-deck-van
    H2T->>TD: LoRa: FlockdarDisplayUpdate\n{total_hits, new_hits, mood} 24 bytes
    Note over TD: Update display face + stats
```

## Node sync flow (Pi Zero 2W)

```mermaid
sequenceDiagram
    participant NIC0 as wlan0\n(monitor mode)
    participant NIC1 as wlan1\n(management)
    participant D as Pi Zero daemon
    participant HUB as Pelican Hub

    loop continuous scan
        NIC0->>D: raw 802.11 frames
        D->>D: detect + store to SQLite
    end

    Note over NIC1: Sees trusted SSID in environment
    NIC1->>HUB: TCP connect
    D->>HUB: POST /api/v1/sync\ngzip NDJSON (records since last_seq)
    HUB->>D: {accepted: N, last_seq: M}
    D->>HUB: GET /api/v1/signatures/latest
    HUB->>D: signatures.toml hash + blob (if changed)
    D->>D: update local signatures if hash differs
    Note over NIC0: Never interrupted — wlan0 stays in monitor mode
```

## Module architecture (daemon)

```mermaid
classDiagram
    class Daemon {
        +modules []Module
        +detCh chan Detection
        +Run(ctx)
    }

    class Module {
        <<interface>>
        +Name() string
        +Run(ctx, ch chan Detection)
    }

    class CoconutModule {
        +interfaces []string
        +Run(ctx, ch)
    }

    class WifiModule {
        +iface string
        +Run(ctx, ch)
    }

    class BLEModule {
        +adapter string
        +Run(ctx, ch)
    }

    class SerialModule {
        +port string
        +Run(ctx, ch)
    }

    class MeshtasticModule {
        +port string
        +Run(ctx, ch)
        +SendDisplayUpdate(FlockdarDisplayUpdate)
    }

    class DetectEngine {
        +Analyze(Detection) *Hit
        +signatures Signatures
    }

    class Store {
        +InsertHit(Hit)
        +InsertGap(Gap)
        +Runs() []Run
        +Hits(filter) []Hit
    }

    Daemon --> Module
    Module <|-- CoconutModule
    Module <|-- WifiModule
    Module <|-- BLEModule
    Module <|-- SerialModule
    Module <|-- MeshtasticModule
    Daemon --> DetectEngine
    Daemon --> Store
```

## Repository layout

```mermaid
graph TD
    ROOT["/"]
    CMD[cmd/]
    INT[internal/]
    WEB[web/]
    ESP[esp32/]
    DOCS[docs/]
    SIG[signatures.toml]
    TASK[Taskfile.yml]

    ROOT --> CMD & INT & WEB & ESP & DOCS & SIG & TASK

    CMD --> DAEMOND[muninnd/\ndaemon binary]
    CMD --> DAEMONCLI[muninn/\nCLI binary]
    CMD --> GENOUI[gen-oui/\nOUI header generator]
    CMD --> GENPINS[gen-pins/\npin header generator]

    INT --> DAEMON[daemon/\ncore + module registry]
    INT --> MODULES[modules/\nwifi · ble · serial\nmeshtastic · coconut]
    INT --> DETECT[detect/\nengine + signatures_gen.go]
    INT --> PROTOCOL[protocol/\nNDJSON types · HMAC]
    INT --> STORE[store/\nSQLite backend]
    INT --> API[api/\nREST + WebSocket]
    INT --> RUNS[runs/\nrun tracking · GPS trace]
    INT --> SYNC[sync/\nloot queue · opportunistic sync]

    WEB --> HTML[index.html]
    WEB --> JS[app.js]
    WEB --> CSS[style.css]

    ESP --> SRC[src/\nC++ firmware]
    ESP --> OUI[oui_list.h\ngenerated]
    ESP --> PINS[pins.h\ngenerated]
    ESP --> PLATFORMIO[platformio.ini]
```

## Build pipeline

```mermaid
flowchart LR
    TOML[signatures.toml] -->|task gen:oui| GENOUI[cmd/gen-oui]
    GENOUI --> OUI[esp32/src/oui_list.h]
    GENOUI --> SGEN[internal/detect/signatures_gen.go]

    PINTOML[pinspec.toml] -->|task gen:pins| GENPINS[cmd/gen-pins]
    GENPINS --> PINS[esp32/src/pins.h]

    SGEN -->|go build| DAEMON[bin/muninnd]
    OUI & PINS -->|pio run| FW[firmware.bin]

    DAEMON -->|task build:arm64| ARM[bin/muninnd-arm64]
    ARM -->|task install TARGET=pi| PI[Pi 4B]
```
