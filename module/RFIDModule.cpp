/*
 * RFIDModule - Session-based Meshtastic module for RDM6300 RFID reading
 *
 * Remote START → continuous UART polling → heartbeat every 5s →
 * auto-stop on tag-gone (10s) or session timeout (30s no tag).
 *
 * Follows the DetectionSensorModule pattern (SinglePortModule + OSThread).
 */

#include "RFIDModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

#include <Arduino.h>
#include <string.h>
#include "nrf_gpio.h"

/* Module singleton */
RFIDModule *rfidModule = nullptr;

RFIDModule::RFIDModule()
    : SinglePortModule("RFID", RFID_PORT),
      concurrency::OSThread("RFIDModule")
{
    rfidModule = this;

    if (RFID_PEER_NODE_ID != 0) {
        LOG_INFO("RFID: Module initialized, peer node: 0x%08lX", (unsigned long)RFID_PEER_NODE_ID);
    } else {
        LOG_WARN("RFID: Module initialized, NO PEER CONFIGURED (broadcast mode)");
    }
    LOG_INFO("RFID: Idle mode - send START from peer to begin session");
}

/* ════════════════════════════════════════════════════════════
 * State machine - called by Meshtastic scheduler
 * ════════════════════════════════════════════════════════════ */

int32_t RFIDModule::runOnce()
{
#ifndef PIN_RFID_POWER
    return disable();
#else
    switch (state) {
    case RFID_IDLE:
        return handleIdle();
    case RFID_PULSE_BOOT:
        return handlePulseBoot();
    case RFID_PULSE_SCAN:
        return handlePulseScan();
    case RFID_PULSE_SLEEP:
        return handlePulseSleep();
    case RFID_POWERING_OFF:
        return handlePoweringOff();
    default:
        setState(RFID_IDLE);
        return RFID_IDLE_POLL_MS;
    }
#endif
}

void RFIDModule::setState(RFIDState newState)
{
    state = newState;
    stateEnteredAt = millis();
}

/* ════════════════════════════════════════════════════════════
 * State handlers
 * ════════════════════════════════════════════════════════════ */

int32_t RFIDModule::handleIdle()
{
#ifdef PIN_RFID_POWER
    /* GPIO diagnostic: check pin state matches output register */
    NRF_GPIO_Type *port = (PIN_RFID_POWER >= 32) ? NRF_P1 : NRF_P0;
    uint32_t pin_in_port = PIN_RFID_POWER & 0x1F;

    uint32_t pin_cnf = port->PIN_CNF[pin_in_port];
    uint32_t out_bit = (port->OUT >> pin_in_port) & 1;

    /* Briefly enable input buffer to read real pad voltage */
    port->PIN_CNF[pin_in_port] = pin_cnf & ~(1UL << 1);
    __DSB();
    __NOP(); __NOP(); __NOP(); __NOP();
    volatile uint32_t r1 = (port->IN >> pin_in_port) & 1;
    volatile uint32_t r2 = (port->IN >> pin_in_port) & 1;
    volatile uint32_t r3 = (port->IN >> pin_in_port) & 1;
    port->PIN_CNF[pin_in_port] = pin_cnf;
    uint32_t actual_pin = (r1 + r2 + r3) >= 2 ? 1 : 0;

    if (pin_cnf != lastPinCnf) {
        LOG_WARN("RFID: GPIO%d config changed: PIN_CNF=0x%08lX (was 0x%08lX)",
                 PIN_RFID_POWER, (unsigned long)pin_cnf, (unsigned long)lastPinCnf);
        lastPinCnf = pin_cnf;
    }

    if (actual_pin != out_bit) {
        pinMismatchCount++;
        if (pinMismatchCount <= 5 || (pinMismatchCount % 20) == 0) {
            LOG_ERROR("RFID: GPIO%d OUTPUT=%d but pin READS %d! (mismatch #%lu)",
                      PIN_RFID_POWER, out_bit, actual_pin, (unsigned long)pinMismatchCount);
        }
    } else if (pinMismatchCount > 0) {
        LOG_INFO("RFID: GPIO%d now matches (OUT=%d, PIN=%d). Resolved after %lu mismatches.",
                 PIN_RFID_POWER, out_bit, actual_pin, (unsigned long)pinMismatchCount);
        pinMismatchCount = 0;
    }

    nrf_gpio_pin_clear(PIN_RFID_POWER);
#endif
    checkSerialInput();
    return RFID_IDLE_POLL_MS;
}

int32_t RFIDModule::handlePulseBoot()
{
    /* RDM6300 has had RFID_PULSE_BOOT_MS to power up — init/flush UART */
    initUART();
    flushUART();
    bufferIndex = 0;

    setState(RFID_PULSE_SCAN);
    return RFID_UART_POLL_MS;
}

int32_t RFIDModule::handlePulseScan()
{
    uint32_t tagId = 0;

    /* Try to read a tag from UART */
    if (readRDM6300(&tagId)) {
        char tagHex[9];
        snprintf(tagHex, sizeof(tagHex), "%08lX", (unsigned long)tagId);

        if (currentTagId[0] == '\0') {
            LOG_INFO("RFID: Tag detected: %s", tagHex);
        } else if (strcmp(tagHex, currentTagId) != 0) {
            LOG_INFO("RFID: Tag changed: %s -> %s", currentTagId, tagHex);
        }
        memcpy(currentTagId, tagHex, sizeof(currentTagId));

        tagEverSeen = true;
        consecutiveMisses = 0;
        sendTagHeartbeat(currentTagId);

        /* Tag found — power off reader, sleep until next pulse */
        nrf_gpio_pin_clear(PIN_RFID_POWER);
        LOG_DEBUG("RFID: Pulse hit, sleeping %ds", RFID_PULSE_SLEEP_MS / 1000);
        setState(RFID_PULSE_SLEEP);
        return RFID_PULSE_SLEEP_MS;
    }

    /* Scan window expired? */
    if (millis() - stateEnteredAt >= RFID_PULSE_SCAN_MS) {
        consecutiveMisses++;

        /* Power off reader for sleep */
        nrf_gpio_pin_clear(PIN_RFID_POWER);

        /* Tag was seen before but now missed too many pulses */
        if (tagEverSeen && consecutiveMisses >= RFID_MISS_MAX) {
            LOG_INFO("RFID: Tag %s gone (%d missed pulses)", currentTagId, RFID_MISS_MAX);
            endSession(RFID_RESP_GONE);
            return 0;
        }

        /* Never saw a tag and session timed out */
        if (!tagEverSeen && (millis() - sessionStartedAt >= RFID_SESSION_TIMEOUT_MS)) {
            LOG_INFO("RFID: No tag detected within %ds", RFID_SESSION_TIMEOUT_MS / 1000);
            endSession(RFID_RESP_NOTAG);
            return 0;
        }

        /* Send miss notification (fire-and-forget) */
        char missPayload[20];
        snprintf(missPayload, sizeof(missPayload), "%s%lu/%d",
                 RFID_RESP_MISS, (unsigned long)consecutiveMisses, RFID_MISS_MAX);
        sendMeshPacket(missPayload, false);

        LOG_DEBUG("RFID: Pulse miss #%lu", (unsigned long)consecutiveMisses);
        setState(RFID_PULSE_SLEEP);
        return RFID_PULSE_SLEEP_MS;
    }

    return RFID_UART_POLL_MS;
}

int32_t RFIDModule::handlePulseSleep()
{
    /* Sleep period over — power on for next pulse */
    nrf_gpio_pin_set(PIN_RFID_POWER);
    setState(RFID_PULSE_BOOT);
    return RFID_PULSE_BOOT_MS;
}

int32_t RFIDModule::handlePoweringOff()
{
    powerOffRFID();
    sessionStartedBy = 0;
    sessionStartedAt = 0;
    consecutiveMisses = 0;
    tagEverSeen = false;
    currentTagId[0] = '\0';

    LOG_DEBUG("RFID: Session ended, returning to idle");
    setState(RFID_IDLE);
    return RFID_IDLE_POLL_MS;
}

/* Helper: end session with a final message and transition to power off */
void RFIDModule::endSession(const char *finalMessage)
{
    sendMeshPacket(finalMessage);
    setState(RFID_POWERING_OFF);
}

/* ════════════════════════════════════════════════════════════
 * Hardware control
 * ════════════════════════════════════════════════════════════ */

void RFIDModule::powerOnRFID()
{
#ifdef PIN_RFID_POWER
    nrf_gpio_cfg(PIN_RFID_POWER,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_H0H1,
                 NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_set(PIN_RFID_POWER);
    LOG_DEBUG("RFID: Power ON (GPIO %d HIGH, high-drive)", PIN_RFID_POWER);
#endif
}

void RFIDModule::powerOffRFID()
{
#ifdef PIN_RFID_POWER
    nrf_gpio_cfg(PIN_RFID_POWER,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_H0H1,
                 NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_clear(PIN_RFID_POWER);
    LOG_DEBUG("RFID: Power OFF (GPIO %d LOW, high-drive)", PIN_RFID_POWER);

    if (uartInitialized) {
        Serial2.end();
        uartInitialized = false;
    }
#endif
}

void RFIDModule::initUART()
{
    if (!uartInitialized) {
        Serial2.setPins(PIN_SERIAL2_RX, PIN_SERIAL2_TX);
        Serial2.begin(RDM6300_BAUD);
        uartInitialized = true;
        LOG_DEBUG("RFID: UART initialized (RX=%d, TX=%d, %d baud)",
                  PIN_SERIAL2_RX, PIN_SERIAL2_TX, RDM6300_BAUD);
    }
}

void RFIDModule::flushUART()
{
    if (uartInitialized) {
        while (Serial2.available()) {
            Serial2.read();
        }
    }
}

/* ════════════════════════════════════════════════════════════
 * Local serial command input
 * ════════════════════════════════════════════════════════════ */

void RFIDModule::checkSerialInput()
{
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (serialCmdLen > 0) {
                serialCmdBuf[serialCmdLen] = '\0';

                if (strcasecmp(serialCmdBuf, RFID_CMD_START) == 0) {
                    if (state != RFID_IDLE) {
                        LOG_WARN("RFID: Already in session (state=%d)", state);
                    } else {
                        LOG_INFO("RFID: Local START - beginning pulse-scan session");
                        sessionStartedBy = 0;
                        sessionStartedAt = millis();
                        consecutiveMisses = 0;
                        tagEverSeen = false;
                        currentTagId[0] = '\0';
                        powerOnRFID();
                        setState(RFID_PULSE_BOOT);
                        setIntervalFromNow(RFID_PULSE_BOOT_MS);
                    }
                } else if (strcasecmp(serialCmdBuf, RFID_CMD_STOP) == 0) {
                    if (state == RFID_IDLE) {
                        LOG_WARN("RFID: No active session to stop");
                    } else {
                        LOG_INFO("RFID: Local STOP - ending session");
                        setState(RFID_POWERING_OFF);
                        setIntervalFromNow(0);
                    }
                } else if (strcasecmp(serialCmdBuf, RFID_CMD_PWRON) == 0) {
                    LOG_INFO("RFID: DEBUG - Forcing power ON (GPIO %d HIGH)", PIN_RFID_POWER);
                    powerOnRFID();
                } else if (strcasecmp(serialCmdBuf, RFID_CMD_PWROFF) == 0) {
                    LOG_INFO("RFID: DEBUG - Forcing power OFF (GPIO %d LOW)", PIN_RFID_POWER);
                    powerOffRFID();
                } else if (strcasecmp(serialCmdBuf, RFID_CMD_STATUS) == 0) {
                    printGPIOStatus();
                } else {
                    LOG_WARN("RFID: Unknown command (use START, STOP, POWERON, POWEROFF, STATUS)");
                }
                serialCmdLen = 0;
            }
            continue;
        }

        if (serialCmdLen < SERIAL_CMD_BUF_SIZE - 1) {
            serialCmdBuf[serialCmdLen++] = c;
        }
    }
}

void RFIDModule::printGPIOStatus()
{
#ifdef PIN_RFID_POWER
    NRF_GPIO_Type *port = (PIN_RFID_POWER >= 32) ? NRF_P1 : NRF_P0;
    uint32_t pin_in_port = PIN_RFID_POWER & 0x1F;

    uint32_t pin_cnf = port->PIN_CNF[pin_in_port];
    uint32_t out_bit = (port->OUT >> pin_in_port) & 1;

    port->PIN_CNF[pin_in_port] = pin_cnf & ~(1UL << 1);
    __DSB();
    __NOP(); __NOP(); __NOP(); __NOP();
    uint32_t actual_pin = (port->IN >> pin_in_port) & 1;
    port->PIN_CNF[pin_in_port] = pin_cnf;

    uint32_t dir = pin_cnf & 1;
    uint32_t drive = (pin_cnf >> 8) & 7;
    uint32_t pull = (pin_cnf >> 2) & 3;
    const char *port_name = (PIN_RFID_POWER >= 32) ? "P1" : "P0";

    LOG_INFO("RFID: === GPIO %d (%s.%02d) STATUS ===", PIN_RFID_POWER, port_name, pin_in_port);
    LOG_INFO("RFID:   PIN_CNF=0x%08lX DIR=%s DRIVE=%s PULL=%s",
             (unsigned long)pin_cnf,
             dir ? "OUT" : "IN",
             drive == 0 ? "S0S1" : drive == 3 ? "H0H1" : "OTHER",
             pull == 0 ? "NONE" : pull == 1 ? "DOWN" : "UP");
    LOG_INFO("RFID:   OUT=%d  Actual PIN=%d  %s",
             out_bit, actual_pin,
             (actual_pin != out_bit) ? "<-- MISMATCH!" : "(OK)");
    LOG_INFO("RFID:   State=%d, session_by=0x%08lX, tag=%s",
             state, (unsigned long)sessionStartedBy,
             currentTagId[0] ? currentTagId : "(none)");
#else
    LOG_INFO("RFID: PIN_RFID_POWER not defined for this variant");
#endif
}

/* ════════════════════════════════════════════════════════════
 * RDM6300 protocol handling
 *
 * Packet format (14 bytes):
 *   Byte 0:     0x02 (STX)
 *   Byte 1-2:   Version (2 ASCII hex chars)
 *   Byte 3-10:  Tag ID (8 ASCII hex chars = 4 bytes)
 *   Byte 11-12: Checksum (2 ASCII hex chars)
 *   Byte 13:    0x03 (ETX)
 * ════════════════════════════════════════════════════════════ */

bool RFIDModule::readRDM6300(uint32_t *tagId)
{
    if (!uartInitialized)
        return false;

    while (Serial2.available()) {
        uint8_t c = Serial2.read();

        if (c == RDM6300_START_BYTE) {
            bufferIndex = 0;
            uartBuffer[bufferIndex++] = c;
        } else if (bufferIndex > 0) {
            uartBuffer[bufferIndex++] = c;

            if (bufferIndex >= RDM6300_PACKET_SIZE) {
                if (uartBuffer[RDM6300_PACKET_SIZE - 1] == RDM6300_END_BYTE) {
                    bool valid = parseRDM6300Packet(uartBuffer, tagId);
                    bufferIndex = 0;
                    return valid;
                }
                bufferIndex = 0;
            }
        }
    }

    return false;
}

bool RFIDModule::parseRDM6300Packet(const uint8_t *packet, uint32_t *tagId)
{
    if (packet[0] != RDM6300_START_BYTE || packet[13] != RDM6300_END_BYTE) {
        LOG_WARN("RFID: Bad packet framing");
        return false;
    }

    uint8_t checksum = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t high = hexCharToValue(packet[1 + i * 2]);
        uint8_t low = hexCharToValue(packet[2 + i * 2]);
        if (high == 0xFF || low == 0xFF) {
            LOG_WARN("RFID: Invalid hex character in packet");
            return false;
        }
        checksum ^= (high << 4) | low;
    }

    uint8_t rxHigh = hexCharToValue(packet[11]);
    uint8_t rxLow = hexCharToValue(packet[12]);
    if (rxHigh == 0xFF || rxLow == 0xFF) {
        LOG_WARN("RFID: Invalid hex in checksum");
        return false;
    }
    uint8_t rxChecksum = (rxHigh << 4) | rxLow;

    if (checksum != rxChecksum) {
        LOG_WARN("RFID: Checksum mismatch (calc=0x%02X, rx=0x%02X)", checksum, rxChecksum);
        return false;
    }

    *tagId = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = hexCharToValue(packet[3 + i]);
        if (nibble == 0xFF) {
            LOG_WARN("RFID: Invalid hex in tag ID");
            return false;
        }
        *tagId = (*tagId << 4) | nibble;
    }

    return true;
}

uint8_t RFIDModule::hexCharToValue(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0xFF;
}

/* ════════════════════════════════════════════════════════════
 * Mesh communication
 * ════════════════════════════════════════════════════════════ */

void RFIDModule::sendMeshPacket(const char *payload, bool wantAck)
{
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        LOG_ERROR("RFID: Failed to allocate mesh packet");
        return;
    }

    /* Set destination: session requester, configured peer, or broadcast */
    if (sessionStartedBy != 0) {
        p->to = sessionStartedBy;
    } else if (RFID_PEER_NODE_ID != 0) {
        p->to = RFID_PEER_NODE_ID;
    } else {
        p->to = NODENUM_BROADCAST;
    }

    p->decoded.payload.size = strlen(payload);
    memcpy(p->decoded.payload.bytes, payload, p->decoded.payload.size);

    p->want_ack = wantAck;
    p->hop_limit = 1;

    LOG_INFO("RFID: TX to %08lX: %s", (unsigned long)p->to, payload);
    service->sendToMesh(p);
}

void RFIDModule::sendTagHeartbeat(const char *tagHex)
{
    char payload[16];
    snprintf(payload, sizeof(payload), "%s%s", RFID_RESP_PREFIX, tagHex);
    sendMeshPacket(payload, false); /* No ACK — periodic, next pulse catches loss */
}

void RFIDModule::sendTagGone()
{
    sendMeshPacket(RFID_RESP_GONE);
}

void RFIDModule::sendNoTagResponse()
{
    sendMeshPacket(RFID_RESP_NOTAG);
}

/* ════════════════════════════════════════════════════════════
 * Receive handler - dispatches START/STOP and tag responses
 * ════════════════════════════════════════════════════════════ */

ProcessMessage RFIDModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (RFID_PEER_NODE_ID != 0 && mp.from != RFID_PEER_NODE_ID) {
        LOG_DEBUG("RFID: Ignoring message from unknown node %08X (peer=%08lX)",
                  mp.from, (unsigned long)RFID_PEER_NODE_ID);
        return ProcessMessage::CONTINUE;
    }

    auto &p = mp.decoded;
    if (p.payload.size == 0)
        return ProcessMessage::CONTINUE;

    char msg[p.payload.size + 1];
    memcpy(msg, p.payload.bytes, p.payload.size);
    msg[p.payload.size] = '\0';

    LOG_INFO("RFID: RX from %08X: %s (RSSI: %d, SNR: %.1f)",
             mp.from, msg, mp.rx_rssi, mp.rx_snr);

    /* ── Handle START command ── */
    if (strcmp(msg, RFID_CMD_START) == 0) {
        if (state != RFID_IDLE) {
            LOG_WARN("RFID: Session already active (state=%d), ignoring START", state);
            return ProcessMessage::CONTINUE;
        }

        LOG_INFO("RFID: Session started by peer %08X", mp.from);
        sessionStartedBy = mp.from;
        sessionStartedAt = millis();
        consecutiveMisses = 0;
        tagEverSeen = false;
        currentTagId[0] = '\0';
        powerOnRFID();
        setState(RFID_PULSE_BOOT);
        setIntervalFromNow(RFID_PULSE_BOOT_MS);
        return ProcessMessage::CONTINUE;
    }

    /* ── Handle STOP command ── */
    if (strcmp(msg, RFID_CMD_STOP) == 0) {
        if (state == RFID_IDLE) {
            LOG_WARN("RFID: No active session to stop");
        } else {
            LOG_INFO("RFID: Session stopped by peer %08X", mp.from);
            setState(RFID_POWERING_OFF);
            setIntervalFromNow(0);
        }
        return ProcessMessage::CONTINUE;
    }

    /* ── Handle POWERON/POWEROFF debug commands ── */
    if (strcmp(msg, RFID_CMD_PWRON) == 0) {
        LOG_INFO("RFID: Remote POWERON from %08X - forcing GPIO HIGH", mp.from);
        powerOnRFID();
        return ProcessMessage::CONTINUE;
    }
    if (strcmp(msg, RFID_CMD_PWROFF) == 0) {
        LOG_INFO("RFID: Remote POWEROFF from %08X - forcing GPIO LOW", mp.from);
        powerOffRFID();
        setState(RFID_IDLE);
        return ProcessMessage::CONTINUE;
    }

    /* ── Handle RFID responses (on receiver side) ── */
    if (strncmp(msg, RFID_RESP_PREFIX, strlen(RFID_RESP_PREFIX)) == 0) {
        const char *data = msg + strlen(RFID_RESP_PREFIX);

        if (strcmp(data, "NOTAG") == 0) {
            LOG_INFO("RFID: Peer reports no tag found (session timeout)");
        } else if (strcmp(data, "GONE") == 0) {
            LOG_INFO("RFID: Peer reports tag left range");
        } else if (strncmp(data, "MISS", 4) == 0) {
            LOG_INFO("RFID: Peer pulse miss: %s", data + 4);
        } else {
            LOG_INFO("RFID: Peer tag heartbeat: %s", data);
        }

#ifdef LED_BUILTIN
        digitalWrite(LED_BUILTIN, LED_STATE_ON);
        delay(50);
        digitalWrite(LED_BUILTIN, !LED_STATE_ON);
#endif
        return ProcessMessage::CONTINUE;
    }

    LOG_WARN("RFID: Unknown message from peer: %s", msg);
    return ProcessMessage::CONTINUE;
}
