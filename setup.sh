#!/bin/bash
#
# RFID-Meshtastic Setup Script
#
# Clones the Meshtastic firmware, creates symlinks for the custom variant
# and RFID module, and patches Modules.cpp to register the module.
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

# ── Step 2: Create variant symlink ──
echo "[2/4] Creating variant symlink..."
cd "${FIRMWARE_DIR}"

# Create parent directory if needed
mkdir -p "$(dirname "${VARIANT_DST}")"

# Remove existing symlink or directory
if [ -L "${VARIANT_DST}" ] || [ -d "${VARIANT_DST}" ]; then
    rm -rf "${VARIANT_DST}"
fi

# Create symlink: firmware/variants/nrf52840/diy/nrf52_promicro_rfid -> ../../variant/
ln -sf "${SCRIPT_DIR}/variant" "${VARIANT_DST}"
echo "  Linked: ${VARIANT_DST} -> ${SCRIPT_DIR}/variant"
echo ""

# ── Step 3: Create module symlinks ──
echo "[3/4] Creating module symlinks..."

# Symlink RFIDModule.h
if [ -L "${MODULE_DST}/RFIDModule.h" ]; then
    rm "${MODULE_DST}/RFIDModule.h"
fi
ln -sf "${SCRIPT_DIR}/module/RFIDModule.h" "${MODULE_DST}/RFIDModule.h"
echo "  Linked: ${MODULE_DST}/RFIDModule.h"

# Symlink RFIDModule.cpp
if [ -L "${MODULE_DST}/RFIDModule.cpp" ]; then
    rm "${MODULE_DST}/RFIDModule.cpp"
fi
ln -sf "${SCRIPT_DIR}/module/RFIDModule.cpp" "${MODULE_DST}/RFIDModule.cpp"
echo "  Linked: ${MODULE_DST}/RFIDModule.cpp"
echo ""

# ── Step 4: Patch Modules.cpp to register RFIDModule ──
echo "[4/4] Patching Modules.cpp for RFID module registration..."

# Check if already patched
if grep -q "RFIDModule" "${MODULES_CPP}"; then
    echo "  Already patched, skipping."
else
    # Add the include at the top of the file (after the last #include)
    # Find the last #include line number and add our include after it
    LAST_INCLUDE_LINE=$(grep -n "^#include" "${MODULES_CPP}" | tail -1 | cut -d: -f1)

    if [ -z "${LAST_INCLUDE_LINE}" ]; then
        echo "  ERROR: Could not find #include lines in Modules.cpp"
        exit 1
    fi

    # Insert our conditional include after the last #include
    sed -i.bak "${LAST_INCLUDE_LINE}a\\
\\
#if defined(NRF52_PROMICRO_RFID)\\
#include \"RFIDModule.h\"\\
#endif" "${MODULES_CPP}"

    # Add module instantiation in setupModules() function
    # Find the line with "new RangeTestModule" or similar module instantiation
    # and add our module before it
    if grep -q "new RangeTestModule" "${MODULES_CPP}"; then
        sed -i.bak "/new RangeTestModule/i\\
#if defined(NRF52_PROMICRO_RFID)\\
    new RFIDModule();\\
#endif" "${MODULES_CPP}"
    else
        # Fallback: find setupModules function and add near the end
        # Look for the closing brace of setupModules
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
echo "    pio run -e nrf52_promicro_rfid"
echo ""
echo "  Or use: ./build.sh"
echo "═══════════════════════════════════════════════"
