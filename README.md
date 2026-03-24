# RFID-Meshtastic Firmware

Custom Meshtastic firmware module for peer-to-peer RFID tag reading and transmission using nRF52840 Pro Micro + Wio SX1262 + RDM6300.

## Hardware

| Component | Function | Interface |
|---|---|---|
| nRF52840 Pro Micro (SuperMini) | MCU | - |
| Wio SX1262 | LoRa radio (Meshtastic) | SPI (CS=D10, RST=D9, BUSY=D8, DIO1=D7) |
| RDM6300 | 125KHz RFID reader | UART 9600 baud (TX=D1, RX=D0) |
| MT3608 + MOSFET switch | Power control for RDM6300 | GPIO D6 (HIGH=on, LOW=off) |
| LiPo Battery | 3.7V power source | RAW pin |

See `../rfid/main.ato` for the complete circuit design.

## How It Works

Both nodes idle by default — RDM6300 is powered off, drawing zero current. Either peer can trigger a scan on the other on-demand.

1. **Node A** wants to read a tag on **Node B's** reader
2. Node A sends a `SCAN` command to Node B (via mesh or local serial console)
3. Node B receives the command, powers on its RDM6300, scans for up to 3 seconds
4. Node B sends the result back: `RFID:0A1B2C3D` (tag found) or `RFID:NOTAG` (no tag)
5. Node B powers off the RDM6300 and returns to idle

Either node can initiate a scan on the other — roles are symmetric.

### Triggering a Scan

- **Via serial console**: Type `SCAN` + Enter in the serial monitor (115200 baud)
- **Via mesh**: The peer node sends a `SCAN` message on `PRIVATE_APP` port (256)

### Protocol Messages

| Message | Direction | Meaning |
|---|---|---|
| `SCAN` | Requester -> Reader | Request peer to scan for RFID tag |
| `RFID:0A1B2C3D` | Reader -> Requester | Tag found (8-hex-char ID) |
| `RFID:NOTAG` | Reader -> Requester | No tag detected within timeout |

## Prerequisites

- [PlatformIO](https://platformio.org/install/cli) (CLI or IDE)
- Git
- Python 3.x

## Setup

```bash
# Clone this repo and run setup
cd rfid-meshtastic
./setup.sh

# Optionally pin to a specific firmware release
./setup.sh --firmware-tag v2.5.6.0
```

The setup script:
1. Clones the Meshtastic firmware (with submodules)
2. Symlinks our custom variant into the firmware tree
3. Symlinks the RFID module source files
4. Patches `Modules.cpp` to register the module

## Build

```bash
./build.sh              # Build firmware
./build.sh upload       # Build and flash via USB
./build.sh clean        # Clean build
./build.sh monitor      # Serial monitor (115200 baud)
```

Or manually:
```bash
cd firmware
pio run -e nrf52_promicro_rfid
```

## Flash

### Option 1: UF2 Bootloader (recommended)
1. Double-tap reset on the Pro Micro to enter UF2 bootloader mode
2. A USB drive (NICENANO or similar) will mount
3. Copy the firmware: `cp firmware/.pio/build/nrf52_promicro_rfid/firmware.uf2 /Volumes/NICENANO/`

### Option 2: PlatformIO Upload
```bash
./build.sh upload
```

## Pin Mapping

nRF52840 GPIO assignments for each Pro Micro board position:

| Function | Pro Micro Pin | nRF52840 GPIO |
|---|---|---|
| UART TX (to RDM6300) | D1/TX (pin 1) | P0.06 |
| UART RX (from RDM6300) | D0/RX (pin 2) | P0.08 |
| RFID Power Control | D6 (pin 9) | P1.00 |
| LoRa DIO1/IRQ | D7 (pin 10) | P0.11 |
| LoRa BUSY | D8 (pin 11) | P1.04 |
| LoRa RESET | D9 (pin 12) | P1.06 |
| LoRa CS/NSS | D10 (pin 13) | P0.09 |
| SPI MOSI | D16 (pin 14) | P0.10 |
| SPI MISO | D14 (pin 15) | P1.11 |
| SPI SCK | D15 (pin 16) | P1.13 |
| Battery Sense | A0 (pin 20) | P0.31 |
| Onboard LED | - | P0.15 |
| 3V3 Enable | - | P0.13 |

## Project Structure

```
rfid-meshtastic/
├── README.md              # This file
├── setup.sh               # One-time setup script
├── build.sh               # Build/flash/monitor shortcuts
├── firmware/              # Meshtastic firmware (git clone, gitignored)
├── variant/               # Custom board variant definition
│   ├── variant.h          # Pin definitions for our hardware
│   ├── variant.cpp        # Pin map + initialization
│   └── platformio.ini     # PlatformIO build environment
└── module/                # Custom RFID module
    ├── RFIDModule.h       # Module class definition
    └── RFIDModule.cpp     # State machine, UART, mesh communication
```

## Serial Output

Connect via serial monitor at 115200 baud. Type `SCAN` + Enter to trigger a remote scan.

On the **requesting** node:
```
RFID: Local SCAN command - requesting peer to scan
RFID: TX to BBBBBBBB: SCAN
RFID: RX from BBBBBBBB: RFID:0A1B2C3D (RSSI: -45, SNR: 9.5)
RFID: Peer scanned tag: 0A1B2C3D
```

On the **scanning** node:
```
RFID: RX from AAAAAAAA: SCAN (RSSI: -42, SNR: 10.0)
RFID: Scan requested by peer AAAAAAAA, powering on reader...
RFID: Reader powered, scanning for tags...
RFID: Tag detected: 0A1B2C3D
RFID: TX to AAAAAAAA: RFID:0A1B2C3D
RFID: Reader powered off, returning to idle
```

## Configuration

Timing constants in `module/RFIDModule.h`:

| Constant | Default | Description |
|---|---|---|
| `RFID_PEER_NODE_ID` | 0 | Paired peer's Meshtastic node ID (0 = broadcast) |
| `RFID_POWER_ON_DELAY_MS` | 200 | RDM6300 boot time after power on |
| `RFID_READ_TIMEOUT_MS` | 3000 | Max wait for tag during scan |
| `RFID_IDLE_POLL_MS` | 500 | Serial input check interval while idle |
