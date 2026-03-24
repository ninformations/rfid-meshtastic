#!/bin/bash
#
# RFID-Meshtastic Setup Script
#
# Clones the Meshtastic firmware, copies the custom variant and RFID module
# files into the firmware tree, and patches Modules.cpp to register the module.
#
# Usage: ./setup.sh [--firmware-tag <tag>]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIRMWARE_DIR="${SCRIPT_DIR}/firmware"
VARIANT_NAME="nrf52_promicro_rfid"
VARIANT_DST="variants/nrf52840/diy/${VARIANT_NAME}"
MODULE_DST="src/modules"
MODULES_CPP="${MODULE_DST}/Modules.cpp"

# Default firmware tag (latest stable)
FIRMWARE_TAG="${1:-}"
if [ "$FIRMWARE_TAG" = "--firmware-tag" ]; then
    FIRMWARE_TAG="${2:-}"
fi

echo "═══════════════════════════════════════════════"
echo "  RFID-Meshtastic Setup"
echo "═══════════════════════════════════════════════"
echo ""

# ── Step 1: Clone Meshtastic firmware ──
if [ -d "${FIRMWARE_DIR}" ]; then
    echo "[1/4] Firmware directory exists, pulling latest..."
    cd "${FIRMWARE_DIR}"
    git pull || true
    git submodule update --init --recursive
else
    echo "[1/4] Cloning Meshtastic firmware..."
    git clone --recursive https://github.com/meshtastic/firmware.git "${FIRMWARE_DIR}"
    cd "${FIRMWARE_DIR}"

    if [ -n "${FIRMWARE_TAG}" ]; then
        echo "  Checking out tag: ${FIRMWARE_TAG}"
        git checkout "${FIRMWARE_TAG}"
        git submodule update --init --recursive
    fi
fi

echo "  Firmware ready at: ${FIRMWARE_DIR}"
echo ""

# ── Step 2: Copy variant files ──
echo "[2/4] Copying variant files..."
cd "${FIRMWARE_DIR}"

# Create parent directory if needed
mkdir -p "${VARIANT_DST}"

# Copy variant files (not symlink — PlatformIO build_src_filter needs real files)
cp -f "${SCRIPT_DIR}/variant/variant.h" "${VARIANT_DST}/variant.h"
cp -f "${SCRIPT_DIR}/variant/variant.cpp" "${VARIANT_DST}/variant.cpp"
cp -f "${SCRIPT_DIR}/variant/platformio.ini" "${VARIANT_DST}/platformio.ini"
echo "  Copied: variant.h, variant.cpp, platformio.ini -> ${VARIANT_DST}/"
echo ""

# ── Step 3: Copy module files ──
echo "[3/4] Copying module files..."

# Copy RFIDModule files (symlinks work fine for src/modules/)
cp -f "${SCRIPT_DIR}/module/RFIDModule.h" "${MODULE_DST}/RFIDModule.h"
cp -f "${SCRIPT_DIR}/module/RFIDModule.cpp" "${MODULE_DST}/RFIDModule.cpp"
echo "  Copied: RFIDModule.h, RFIDModule.cpp -> ${MODULE_DST}/"
echo ""

# ── Step 4: Patch Modules.cpp to register RFIDModule ──
echo "[4/4] Patching Modules.cpp for RFID module registration..."

# Check if already patched
if grep -q "RFIDModule" "${MODULES_CPP}"; then
    echo "  Already patched, skipping."
else
    # Add the include right before the setupModules() function definition
    # This avoids inserting inside any #if guards around other includes
    SETUP_LINE=$(grep -n "^void setupModules" "${MODULES_CPP}" | head -1 | cut -d: -f1)

    if [ -z "${SETUP_LINE}" ]; then
        echo "  ERROR: Could not find setupModules() in Modules.cpp"
        exit 1
    fi

    # Insert our conditional include before setupModules()
    sed -i.bak "${SETUP_LINE}i\\
#if defined(NRF52_PROMICRO_RFID)\\
#include \"RFIDModule.h\"\\
#endif\\
" "${MODULES_CPP}"

    # Add module instantiation in setupModules() function
    # Insert before the routing module (which must be last) or before closing brace
    if grep -q "routingModule = new RoutingModule" "${MODULES_CPP}"; then
        sed -i.bak "/routingModule = new RoutingModule/i\\
#if defined(NRF52_PROMICRO_RFID)\\
    new RFIDModule();\\
#endif" "${MODULES_CPP}"
    else
        # Fallback: insert before the closing brace of setupModules
        SETUP_END=$(grep -n "^}" "${MODULES_CPP}" | tail -1 | cut -d: -f1)
        sed -i.bak "${SETUP_END}i\\
\\
#if defined(NRF52_PROMICRO_RFID)\\
    new RFIDModule();\\
#endif" "${MODULES_CPP}"
    fi

    # Clean up backup file
    rm -f "${MODULES_CPP}.bak"
    echo "  Patched: Added RFIDModule registration (guarded by NRF52_PROMICRO_RFID)"
fi

echo ""
echo "═══════════════════════════════════════════════"
echo "  Setup complete!"
echo ""
echo "  To build:"
echo "    cd firmware"
echo "    uv run pio run -e nrf52_promicro_rfid"
echo ""
echo "  Or use: ./build.sh"
echo "═══════════════════════════════════════════════"
