/*
 * RFIDModule - Meshtastic module for on-demand RDM6300 RFID reading
 *
 * Both nodes idle by default (RDM6300 powered off). Either peer can
 * trigger a scan on the other via a SCAN command over the mesh, or
 * locally via serial console ("SCAN" + Enter).
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
    LOG_INFO("RFID: Idle mode - type SCAN in serial or send SCAN from peer to trigger");
}

/* ════════════════════════════════════════════════════════════
 * State machine - called by Meshtastic scheduler
 * ════════════════════════════════════════════════════════════ */

int32_t RFIDModule::runOnce()
{
#ifndef PIN_RFID_POWER
    /* No RFID hardware on this variant - disable module */
    return disable();
#else
    switch (state) {
    case RFID_IDLE:
        return handleIdle();
    case RFID_POWERING_ON:
        return handlePoweringOn();
    case RFID_SCANNING:
        return handleScanning();
    case RFID_SENDING:
        return handleSending();
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
    /*
     * Low power idle: RDM6300 is off.
     * Only check for local serial "SCAN" commands.
     * Mesh SCAN commands are handled in handleReceived().
     */
    checkSerialInput();
    return RFID_IDLE_POLL_MS;
}

int32_t RFIDModule::handlePoweringOn()
{
    /* RDM6300 needs ~200ms after power-on to stabilize */
    initUART();
    flushUART();
    bufferIndex = 0;
    tagFound = false;
    lastTagId = 0;

    LOG_INFO("RFID: Reader powered, scanning for tags...");
    setState(RFID_SCANNING);
    return 50; /* Poll UART every 50ms */
}

int32_t RFIDModule::handleScanning()
{
    uint32_t tagId = 0;

    if (readRDM6300(&tagId)) {
        LOG_INFO("RFID: Tag detected: %08lX", (unsigned long)tagId);
        lastTagId = tagId;
        tagFound = true;
        setState(RFID_SENDING);
        return 10; /* Send immediately */
    }

    /* Check for scan timeout */
    if (millis() - stateEnteredAt >= RFID_READ_TIMEOUT_MS) {
        LOG_DEBUG("RFID: No tag found within %dms timeout", RFID_READ_TIMEOUT_MS);
        tagFound = false;
        setState(RFID_SENDING);
        return 10;
    }

    return 50; /* Keep polling UART */
}

int32_t RFIDModule::handleSending()
{
    if (tagFound) {
        sendTagResponse(lastTagId);
    } else {
        sendNoTagResponse();
    }

    setState(RFID_POWERING_OFF);
    return RFID_POWER_OFF_DELAY_MS;
}

int32_t RFIDModule::handlePoweringOff()
{
    powerOffRFID();

    LOG_DEBUG("RFID: Reader powered off, returning to idle");
    setState(RFID_IDLE);
    return RFID_IDLE_POLL_MS;
}

/* ════════════════════════════════════════════════════════════
 * Hardware control
 * ════════════════════════════════════════════════════════════ */

void RFIDModule::powerOnRFID()
{
#ifdef PIN_RFID_POWER
    pinMode(PIN_RFID_POWER, OUTPUT);
    digitalWrite(PIN_RFID_POWER, RFID_POWER_ON);
    LOG_DEBUG("RFID: Power ON (GPIO %d HIGH)", PIN_RFID_POWER);
#endif
}

void RFIDModule::powerOffRFID()
{
#ifdef PIN_RFID_POWER
    digitalWrite(PIN_RFID_POWER, RFID_POWER_OFF);
    LOG_DEBUG("RFID: Power OFF (GPIO %d LOW)", PIN_RFID_POWER);

    /* End Serial2 to release UART pins and save power */
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
 *
 * Type "SCAN" + Enter in serial monitor to send a scan request
 * to the paired peer node.
 * ════════════════════════════════════════════════════════════ */

void RFIDModule::checkSerialInput()
{
    while (Serial.available()) {
        char c = Serial.read();

        /* Newline terminates command */
        if (c == '\n' || c == '\r') {
            if (serialCmdLen > 0) {
                serialCmdBuf[serialCmdLen] = '\0';

                if (strcasecmp(serialCmdBuf, RFID_CMD_SCAN) == 0) {
                    LOG_INFO("RFID: Local SCAN command - requesting peer to scan");
                    sendScanRequest();
                } else if (strcasecmp(serialCmdBuf, RFID_CMD_LOCAL) == 0) {
                    LOG_INFO("RFID: Local test - powering on RFID reader...");
                    powerOnRFID();
                    setState(RFID_POWERING_ON);
                    setIntervalFromNow(RFID_POWER_ON_DELAY_MS);
                } else {
                    LOG_WARN("RFID: Unknown command: '%s' (use SCAN or LOCALSCAN)", serialCmdBuf);
                }
                serialCmdLen = 0;
            }
            continue;
        }

        /* Accumulate characters */
        if (serialCmdLen < SERIAL_CMD_BUF_SIZE - 1) {
            serialCmdBuf[serialCmdLen++] = c;
        }
    }
}

/* ════════════════════════════════════════════════════════════
 * RDM6300 protocol handling
 *
 * Packet format (14 bytes):
 *   Byte 0:     0x02 (STX - start)
 *   Byte 1-2:   Version (2 ASCII hex chars)
 *   Byte 3-10:  Tag ID (8 ASCII hex chars = 4 bytes)
 *   Byte 11-12: Checksum (2 ASCII hex chars)
 *   Byte 13:    0x03 (ETX - end)
 *
 * Checksum: XOR of all 5 data bytes (version + tag ID)
 * ════════════════════════════════════════════════════════════ */

bool RFIDModule::readRDM6300(uint32_t *tagId)
{
    if (!uartInitialized)
        return false;

    while (Serial2.available()) {
        uint8_t c = Serial2.read();

        if (c == RDM6300_START_BYTE) {
            /* Start of new packet */
            bufferIndex = 0;
            uartBuffer[bufferIndex++] = c;
        } else if (bufferIndex > 0) {
            uartBuffer[bufferIndex++] = c;

            if (bufferIndex >= RDM6300_PACKET_SIZE) {
                /* Full packet received */
                if (uartBuffer[RDM6300_PACKET_SIZE - 1] == RDM6300_END_BYTE) {
                    bool valid = parseRDM6300Packet(uartBuffer, tagId);
                    bufferIndex = 0;
                    return valid;
                }
                /* Bad packet end byte, reset */
                bufferIndex = 0;
            }
        }
        /* Ignore bytes outside of a packet (before STX) */
    }

    return false;
}

bool RFIDModule::parseRDM6300Packet(const uint8_t *packet, uint32_t *tagId)
{
    /* Verify framing */
    if (packet[0] != RDM6300_START_BYTE || packet[13] != RDM6300_END_BYTE) {
        LOG_WARN("RFID: Bad packet framing");
        return false;
    }

    /*
     * Calculate checksum: XOR of 5 data bytes
     * Data bytes are encoded as 10 ASCII hex characters (bytes 1-10)
     * Each pair of hex chars forms one byte: [1,2], [3,4], [5,6], [7,8], [9,10]
     */
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

    /* Verify checksum against bytes 11-12 */
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

    /*
     * Extract 4-byte tag ID from bytes 3-10 (skip 2-char version prefix)
     * 8 ASCII hex chars = 32-bit tag ID
     */
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
    return 0xFF; /* Invalid hex character */
}

/* ════════════════════════════════════════════════════════════
 * Mesh communication
 * ════════════════════════════════════════════════════════════ */

void RFIDModule::sendMeshPacket(const char *payload)
{
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        LOG_ERROR("RFID: Failed to allocate mesh packet");
        return;
    }

    /* Set packet destination */
    if (RFID_PEER_NODE_ID != 0) {
        p->to = RFID_PEER_NODE_ID;
    } else {
        p->to = NODENUM_BROADCAST;
    }

    /* Copy payload */
    p->decoded.payload.size = strlen(payload);
    memcpy(p->decoded.payload.bytes, payload, p->decoded.payload.size);

    p->want_ack = true;
    p->hop_limit = 1;

    LOG_INFO("RFID: TX to %08lX: %s",
             (unsigned long)p->to, payload);

    service->sendToMesh(p);
}

void RFIDModule::sendScanRequest()
{
    if (RFID_PEER_NODE_ID == 0) {
        LOG_WARN("RFID: Cannot send SCAN - no peer configured");
        return;
    }
    sendMeshPacket(RFID_CMD_SCAN);
}

void RFIDModule::sendTagResponse(uint32_t tagId)
{
    char payload[16];
    snprintf(payload, sizeof(payload), "%s%08lX", RFID_RESP_PREFIX, (unsigned long)tagId);
    sendMeshPacket(payload);
}

void RFIDModule::sendNoTagResponse()
{
    sendMeshPacket(RFID_RESP_NOTAG);
}

/* ════════════════════════════════════════════════════════════
 * Receive handler - dispatches SCAN commands and tag responses
 * ════════════════════════════════════════════════════════════ */

ProcessMessage RFIDModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    /* If a peer is configured, only accept messages from that peer */
    if (RFID_PEER_NODE_ID != 0 && mp.from != RFID_PEER_NODE_ID) {
        LOG_DEBUG("RFID: Ignoring message from unknown node %08X (peer=%08lX)",
                  mp.from, (unsigned long)RFID_PEER_NODE_ID);
        return ProcessMessage::CONTINUE;
    }

    auto &p = mp.decoded;
    if (p.payload.size == 0)
        return ProcessMessage::CONTINUE;

    /* Extract payload as string */
    char msg[p.payload.size + 1];
    memcpy(msg, p.payload.bytes, p.payload.size);
    msg[p.payload.size] = '\0';

    LOG_INFO("RFID: RX from %08X: %s (RSSI: %d, SNR: %.1f)",
             mp.from, msg, mp.rx_rssi, mp.rx_snr);

    /* ── Handle SCAN command from peer ── */
    if (strcmp(msg, RFID_CMD_SCAN) == 0) {
        if (state != RFID_IDLE) {
            LOG_WARN("RFID: Scan requested but already busy (state=%d)", state);
            return ProcessMessage::CONTINUE;
        }

        LOG_INFO("RFID: Scan requested by peer %08X, powering on reader...", mp.from);
        powerOnRFID();
        setState(RFID_POWERING_ON);
        /* Wake the OSThread immediately to process the power-on */
        setIntervalFromNow(RFID_POWER_ON_DELAY_MS);
        return ProcessMessage::CONTINUE;
    }

    /* ── Handle RFID tag response from peer ── */
    if (strncmp(msg, RFID_RESP_PREFIX, strlen(RFID_RESP_PREFIX)) == 0) {
        const char *data = msg + strlen(RFID_RESP_PREFIX);

        if (strcmp(data, "NOTAG") == 0) {
            LOG_INFO("RFID: Peer reports no tag found");
        } else {
            LOG_INFO("RFID: Peer scanned tag: %s", data);
        }

        /* Flash LED to indicate response received */
#ifdef LED_BUILTIN
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, LED_STATE_ON);
            delay(100);
            digitalWrite(LED_BUILTIN, !LED_STATE_ON);
            delay(100);
        }
#endif
        return ProcessMessage::CONTINUE;
    }

    LOG_WARN("RFID: Unknown message from peer: %s", msg);
    return ProcessMessage::CONTINUE;
}
