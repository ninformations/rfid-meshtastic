#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/*
 * RFIDModule - Custom Meshtastic module for RDM6300 RFID tag reading
 *
 * On-demand P2P RFID scanning. Both nodes idle by default (zero power to
 * the RDM6300). Either peer can trigger a scan on the other by sending a
 * SCAN command over the mesh. The scanned node powers the reader, reads
 * the tag, sends the result back, and powers off.
 *
 * Protocol (on PRIVATE_APP port 256):
 *   "SCAN"            - Request peer to scan for an RFID tag
 *   "RFID:<8-hex-id>" - Tag data response (e.g. "RFID:0A1B2C3D")
 *   "RFID:NOTAG"      - No tag found within timeout
 *
 * Trigger a scan request via serial console: type "SCAN" + Enter
 *
 * State machine:
 *   IDLE ──(SCAN cmd from peer)──→ POWERING_ON → SCANNING → SENDING → POWERING_OFF → IDLE
 *                                                    └─(timeout)──→ POWERING_OFF → IDLE
 */

/* Use Meshtastic PRIVATE_APP port for custom RFID data */
#define RFID_PORT meshtastic_PortNum_PRIVATE_APP

/* RDM6300 UART protocol constants */
#define RDM6300_PACKET_SIZE 14
#define RDM6300_START_BYTE 0x02
#define RDM6300_END_BYTE 0x03
#define RDM6300_BAUD 9600

/*
 * Peer node ID for P2P communication.
 * Set this to the Meshtastic node ID (hex) of the OTHER node in the pair.
 *
 * How to find a node's ID:
 *   1. Flash Meshtastic firmware to the node
 *   2. Open serial monitor (115200 baud)
 *   3. The node ID is printed at boot, e.g. "Node ID: 0x12345678"
 *   4. Or use the Meshtastic CLI: meshtastic --info
 *
 * Build workflow:
 *   - Flash both nodes first with RFID_PEER_NODE_ID = 0 to discover their IDs
 *   - Then rebuild each node with the other's ID:
 *     Node A (ID 0xAAAAAAAA): set RFID_PEER_NODE_ID to 0xBBBBBBBB
 *     Node B (ID 0xBBBBBBBB): set RFID_PEER_NODE_ID to 0xAAAAAAAA
 *
 * Set to 0 to broadcast to all nodes (unpaired/discovery mode).
 */
#ifndef RFID_PEER_NODE_ID
#define RFID_PEER_NODE_ID 0
#endif

/* Mesh command strings */
#define RFID_CMD_SCAN "SCAN"         /* Request peer to scan */
#define RFID_CMD_LOCAL "LOCALSCAN"   /* Test local RFID reader (serial only) */
#define RFID_CMD_PWRON "POWERON"    /* Force RFID power ON (debug) */
#define RFID_CMD_PWROFF "POWEROFF"  /* Force RFID power OFF (debug) */
#define RFID_RESP_PREFIX "RFID:"     /* Tag data response prefix */
#define RFID_RESP_NOTAG "RFID:NOTAG" /* No tag found response */

/* Timing configuration (milliseconds) */
#define RFID_POWER_ON_DELAY_MS 200   /* Wait for RDM6300 to boot after power on */
#define RFID_READ_TIMEOUT_MS 3000    /* Max time to wait for a tag during scan */
#define RFID_POWER_OFF_DELAY_MS 100  /* Brief delay before returning to idle */
#define RFID_IDLE_POLL_MS 500        /* How often to check serial input while idle */

/* Serial input buffer for local commands */
#define SERIAL_CMD_BUF_SIZE 32

/* Module states */
enum RFIDState {
    RFID_IDLE,         /* Low power: RDM6300 off, waiting for SCAN command */
    RFID_POWERING_ON,  /* GPIO HIGH, waiting for RDM6300 to boot */
    RFID_SCANNING,     /* UART active, polling for tag data */
    RFID_SENDING,      /* Tag detected (or timeout), sending result to peer */
    RFID_POWERING_OFF  /* Shutting down RDM6300 before returning to idle */
};

class RFIDModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    RFIDModule();

  protected:
    /* OSThread - periodic execution via the Meshtastic scheduler */
    virtual int32_t runOnce() override;

    /* SinglePortModule - handle incoming messages from mesh */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    virtual bool wantUIFrame() { return false; }

  private:
    /* Current state machine state */
    RFIDState state = RFID_IDLE;

    /* Timestamp when current state was entered (millis) */
    uint32_t stateEnteredAt = 0;

    /* Last tag ID read during current scan */
    uint32_t lastTagId = 0;

    /* Whether a tag was found during current scan */
    bool tagFound = false;

    /* UART receive buffer for RDM6300 packets */
    uint8_t uartBuffer[RDM6300_PACKET_SIZE];
    uint8_t bufferIndex = 0;

    /* Whether Serial2 has been initialized */
    bool uartInitialized = false;

    /* Serial input buffer for local "SCAN" command */
    char serialCmdBuf[SERIAL_CMD_BUF_SIZE];
    uint8_t serialCmdLen = 0;

    /* ── Hardware control ── */
    void powerOnRFID();
    void powerOffRFID();
    void initUART();
    void flushUART();

    /* ── RDM6300 protocol ── */
    bool readRDM6300(uint32_t *tagId);
    bool parseRDM6300Packet(const uint8_t *packet, uint32_t *tagId);
    uint8_t hexCharToValue(uint8_t c);

    /* ── Mesh communication ── */
    void sendMeshPacket(const char *payload);
    void sendScanRequest();
    void sendTagResponse(uint32_t tagId);
    void sendNoTagResponse();

    /* ── Local serial command input ── */
    void checkSerialInput();

    /* ── State handlers (return ms until next runOnce call) ── */
    int32_t handleIdle();
    int32_t handlePoweringOn();
    int32_t handleScanning();
    int32_t handleSending();
    int32_t handlePoweringOff();

    /* Transition to a new state */
    void setState(RFIDState newState);
};

extern RFIDModule *rfidModule;
