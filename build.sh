#!/bin/bash
#
# RFID-Meshtastic Build Script
#
# Builds the firmware for the nrf52_promicro_rfid target.
# Run setup.sh first if you haven't already.
#
# Usage:
#   ./build.sh              # Build firmware
#   ./build.sh upload       # Build and flash via USB
#   ./build.sh clean        # Clean build artifacts
#   ./build.sh monitor      # Open serial monitor (115200 baud)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIRMWARE_DIR="${SCRIPT_DIR}/firmware"
TARGET="nrf52_promicro_rfid"

if [ ! -d "${FIRMWARE_DIR}" ]; then
    echo "ERROR: Firmware directory not found. Run ./setup.sh first."
    exit 1
fi

cd "${FIRMWARE_DIR}"

case "${1:-build}" in
    build)
        echo "Building ${TARGET}..."
        pio run -e "${TARGET}"
        echo ""
        echo "Build complete. Firmware at:"
        echo "  .pio/build/${TARGET}/firmware.uf2"
        ;;
    upload)
        echo "Building and uploading ${TARGET}..."
        pio run -e "${TARGET}" -t upload
        ;;
    clean)
        echo "Cleaning ${TARGET} build..."
        pio run -e "${TARGET}" -t clean
        ;;
    monitor)
        echo "Opening serial monitor (115200 baud)..."
        pio device monitor -b 115200
        ;;
    *)
        echo "Usage: $0 [build|upload|clean|monitor]"
        exit 1
        ;;
esac
