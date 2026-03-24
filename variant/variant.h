/*
 * RFID-Meshtastic Custom Variant
 * Board: nRF52840 Pro Micro (SuperMini / nice!nano compatible)
 * Radio: Wio SX1262 LoRa module (TCXO, DIO2 RF switch)
 * RFID:  RDM6300 125KHz reader (UART 9600, 5V via boost + MOSFET switch)
 *
 * Pin mapping derived from the nice!nano v2 / SuperMini Pro Micro pinout
 * and the existing Meshtastic nrf52_promicro_diy_tcxo variant.
 *
 * Physical pin table (nRF52840 GPIO at each Pro Micro position):
 *
 *   Left (top→bottom)           Right (top→bottom)
 *   ─────────────────           ──────────────────
 *   GND                         VBAT (RAW)
 *   P0.06  D1/TX  Serial2 RX   VBAT
 *   P0.08  D0/RX  Serial2 TX   GND
 *   GND                         RESET
 *   GND                         P0.13  3V3 Enable
 *   P0.17  D2                   P0.31  BATTERY_PIN (A0)
 *   P0.20  D3                   P0.29  (A1)
 *   P0.22  D4                   P0.02  (A2)
 *   P0.24  D5                   P1.15  (A3)
 *   P1.00  D6  RFID_POWER      P1.13  D15/SCK
 *   P0.11  D7  LORA_DIO1       P1.11  D14/MISO
 *   P1.04  D8  LORA_BUSY       P0.10  D16/MOSI
 *   P1.06  D9  LORA_RESET      P0.09  D10  LORA_CS
 */

#ifndef _VARIANT_NRF52_PROMICRO_RFID_
#define _VARIANT_NRF52_PROMICRO_RFID_

#include "WVariant.h"

#define VARIANT_MCK (64000000ul)

/* Use low-frequency RC oscillator (no 32kHz crystal on SuperMini clone) */
#define USE_LFRC

/* ──────────────────────────────────────────────
 * GPIO count
 * ────────────────────────────────────────────── */
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

/* ──────────────────────────────────────────────
 * 3.3V peripheral enable
 * nice!nano / SuperMini uses P0.13 to control the 3.3V regulator
 * ────────────────────────────────────────────── */
#define PIN_3V3_EN (0 + 13) // P0.13

/* ──────────────────────────────────────────────
 * LED
 * ────────────────────────────────────────────── */
#define PIN_LED1 (0 + 15) // P0.15 (onboard blue LED)
#undef LED_BUILTIN
#define LED_BUILTIN PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 1

/* No user button defined (all GPIO used for peripherals) */
// #define BUTTON_PIN ...

/* ──────────────────────────────────────────────
 * Battery voltage sensing
 * ────────────────────────────────────────────── */
#define BATTERY_PIN (0 + 31) // P0.31 at A0 position
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define ADC_MULTIPLIER 2.0f // Voltage divider on board: 2:1

/* ──────────────────────────────────────────────
 * SPI - Standard Pro Micro SPI positions
 * Used for Wio SX1262 LoRa module
 * ────────────────────────────────────────────── */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MOSI (0 + 10)  // P0.10 (gpio 10) at D16/MOSI position
#define PIN_SPI_MISO (32 + 11) // P1.11 (gpio 43) at D14/MISO position
#define PIN_SPI_SCK (32 + 13)  // P1.13 (gpio 45) at D15/SCK position

static const uint8_t SS = (0 + 9);    // P0.09 - LoRa CS
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/* ──────────────────────────────────────────────
 * UART - Serial2 for RDM6300 RFID reader
 * RDM6300: 9600 baud, sends 14-byte packets
 * Level shifted: 5V TX → voltage divider → 3.3V RX
 * ────────────────────────────────────────────── */
/* Serial1 (required by Arduino framework, used for USB serial console) */
#define PIN_SERIAL1_RX (0 + 8) // Shared with Serial2 RX
#define PIN_SERIAL1_TX (0 + 6) // Shared with Serial2 TX

/* Serial2 for RDM6300 RFID reader */
#define PIN_SERIAL2_RX (0 + 8) // P0.08 (gpio 8) at D0/RX position - data FROM RDM6300
#define PIN_SERIAL2_TX (0 + 6) // P0.06 (gpio 6) at D1/TX position - data TO RDM6300

/* ──────────────────────────────────────────────
 * LoRa Radio - Wio SX1262
 * ────────────────────────────────────────────── */
#define USE_SX1262

/* SPI chip select */
#define LORA_CS (0 + 9) // P0.09 (gpio 9) at D10 position

/* Control pins */
#define LORA_DIO1 (0 + 11)  // P0.11 (gpio 11) at D7 position - interrupt/IRQ
#define LORA_RESET (32 + 6) // P1.06 (gpio 38) at D9 position
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI

/* SX1262-specific configuration */
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY (32 + 4) // P1.04 (gpio 36) at D8 position
#define SX126X_RESET LORA_RESET

/* Wio SX1262 has onboard TCXO controlled by DIO3 at 1.8V */
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

/* Wio SX1262 uses DIO2 internally for TX/RX RF switching */
#define SX126X_DIO2_AS_RF_SWITCH

/* No external RXEN/TXEN pins needed (handled by DIO2 internally) */
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_TXEN RADIOLIB_NC

/* ──────────────────────────────────────────────
 * RFID Power Control
 * GPIO controls both the MT3608 boost converter EN pin
 * and the high-side MOSFET power switch.
 * HIGH = RDM6300 powered (5V), LOW = RDM6300 off (default)
 * ────────────────────────────────────────────── */
#define PIN_RFID_POWER (32 + 0) // P1.00 (gpio 32) at D6 position
#define RFID_POWER_ON HIGH
#define RFID_POWER_OFF LOW

/* ──────────────────────────────────────────────
 * I2C - Not used on this board
 * D7/D8 positions are used for LoRa DIO1/BUSY
 * Define on unused pins to avoid build errors
 * ────────────────────────────────────────────── */
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 17) // P0.17 (D2 position) - not physically connected to I2C
#define PIN_WIRE_SCL (0 + 20) // P0.20 (D3 position) - not physically connected to I2C

/* ──────────────────────────────────────────────
 * No GPS on this board
 * ────────────────────────────────────────────── */
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN -1
#define GPS_TX_PIN -1

/* ──────────────────────────────────────────────
 * No display on this board
 * ────────────────────────────────────────────── */
// No OLED/TFT defines

#endif /* _VARIANT_NRF52_PROMICRO_RFID_ */
