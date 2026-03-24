/*
 * RFID-Meshtastic Custom Variant
 * nRF52840 Pro Micro (SuperMini / nice!nano compatible)
 *
 * Identity pin map: Arduino pin N = nRF52840 GPIO N
 * This is the standard approach for Pro Micro nRF52840 variants in Meshtastic.
 */

#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

/*
 * Identity pin mapping: g_ADigitalPinMap[n] = n
 * nRF52840 has GPIO P0.00-P0.31 (0-31) and P1.00-P1.15 (32-47)
 */
const uint32_t g_ADigitalPinMap[PINS_COUNT] = {
    // P0.00 - P0.31
    0,  1,  2,  3,  4,  5,  6,  7,
    8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    // P1.00 - P1.15
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant()
{
    /*
     * Enable 3.3V regulator output (P0.13)
     * On nice!nano / SuperMini, this pin controls the onboard 3.3V regulator
     * that powers external peripherals via the VCC pin.
     */
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);

    /*
     * Set RFID power OFF by default (P1.00)
     * The RDM6300 is only powered during active scan cycles to save ~1.5mA.
     * GPIO controls both the MT3608 boost converter EN and the MOSFET switch.
     */
    pinMode(PIN_RFID_POWER, OUTPUT);
    digitalWrite(PIN_RFID_POWER, RFID_POWER_OFF);
}
