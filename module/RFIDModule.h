#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/*
 * RFIDModule - Session-based Meshtastic module for RDM6300 RFID tag reading
 *
 * Remote START → power on reader → continuous UART polling → heartbeat
 * reports every 5s while tag is present → auto-stop when tag leaves
 * range (10s timeout) → power off reader.
 *
 * Protocol (on PRIVATE_APP port 256):
 *   "START"           - Begin reading session (remote only)
 *   "STOP"            - End session immediately (remote only)
 *   "RFID:<8-hex-id>" - Heartbeat: tag is in range (sent every 5s)
 *   "RFID:GONE"       - Tag left range, session auto-ending
 *   "RFID:NOTAG"      - Session timeout, no tag ever detected
 *   "RFID:MISS<n>/<max>" - Pulse scan miss (no tag this cycle)
 *
 * Debug commands (serial + remote):
 *   "POWERON"  / "POWEROFF" - Direct GPIO control
 *   "STATUS"                - Dump GPIO diagnostic info (serial only)
 *
 * State machine:
 *   IDLE ──(START)──→ POWERING_ON ──(200ms)──→ LISTENING
 *                                                  │
 *                                          tag detected?
 *                                         no / yes
 *                                        ↓       ↓
 *                           (timeout 30s)    READING ←──┐
 *                                ↓              │       │
 *                          POWERING_OFF    heartbeat    tag still
 *                                ↓          every 5s    present?
 *                              IDLE             │       │
 *                                               └───────┘
 *                                                   │
 *                                              no tag for 10s
 *                                                   ↓
 *                                          send RFID:GONE
 *                                                   ↓
 *                                             POWERING_OFF → IDLE
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
 * Set to 0 to broadcast (unpaired/discovery mode).
 */
#ifndef RFID_PEER_NODE_ID
#define RFID_PEER_NODE_ID 0
#endif

/* Mesh command strings */
#define RFID_CMD_START "START"       /* Begin reading session */
#define RFID_CMD_STOP "STOP"         /* End session immediately */
#define RFID_CMD_PWRON "POWERON"     /* Force RFID power ON (debug) */
#define RFID_CMD_PWROFF "POWEROFF"   /* Force RFID power OFF (debug) */
#define RFID_CMD_STATUS "STATUS"     /* Dump GPIO diagnostic info */
#define RFID_RESP_PREFIX "RFID:"     /* Tag data response prefix */
#define RFID_RESP_NOTAG "RFID:NOTAG" /* No tag found (session timeout) */
#define RFID_RESP_GONE "RFID:GONE"   /* Tag left range */
#define RFID_RESP_MISS "RFID:MISS"   /* Pulse scan miss (no tag this cycle) */

/* Timing configuration (milliseconds) */
#define RFID_PULSE_BOOT_MS 200           /* Wait for RDM6300 carrier to start */
#define RFID_PULSE_SCAN_MS 400           /* UART poll window per pulse */
#define RFID_PULSE_SLEEP_MS 3000         /* Sleep between pulses (reader OFF) */
#define RFID_MISS_MAX 5                  /* Consecutive missed pulses → tag gone */
#define RFID_SESSION_TIMEOUT_MS 30000    /* No tag ever → end session */
#define RFID_UART_POLL_MS 50             /* UART check interval during scan */
#define RFID_IDLE_POLL_MS 500            /* Serial check while idle */

/* Serial input buffer for local commands */
#define SERIAL_CMD_BUF_SIZE 32

/* Module states */
enum RFIDState {
    RFID_IDLE,          /* Reader OFF, waiting for START */
    RFID_PULSE_BOOT,    /* GPIO HIGH, waiting 200ms for RDM6300 carrier */
    RFID_PULSE_SCAN,    /* Polling UART for tag (max 400ms) */
    RFID_PULSE_SLEEP,   /* GPIO LOW, sleeping between pulses */
    RFID_POWERING_OFF   /* Shutting down, end session */
};

class RFIDModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    RFIDModule();

  protected:
    virtual int32_t runOnce() override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantUIFrame() { return false; }

  private:
    /* Current state */
    RFIDState state = RFID_IDLE;
    uint32_t stateEnteredAt = 0;

    /* Session tracking */
    uint32_t sessionStartedBy = 0;   /* NodeNum of remote requester */
    uint32_t sessionStartedAt = 0;   /* millis() when session began */
    uint32_t consecutiveMisses = 0;  /* Pulses in a row with no tag */
    bool tagEverSeen = false;        /* Ever detected a tag this session */
    char currentTagId[9] = {0};      /* 8 hex chars + null */

    /* GPIO diagnostic */
    uint32_t lastPinCnf = 0;
    uint32_t pinMismatchCount = 0;

    /* UART receive buffer for RDM6300 packets */
    uint8_t uartBuffer[RDM6300_PACKET_SIZE];
    uint8_t bufferIndex = 0;
    bool uartInitialized = false;

    /* Serial input buffer */
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
    void sendMeshPacket(const char *payload, bool wantAck = true);
    void sendTagHeartbeat(const char *tagHex);
    void sendTagGone();
    void sendNoTagResponse();

    /* ── Local serial command input ── */
    void checkSerialInput();
    void printGPIOStatus();

    /* ── State handlers ── */
    int32_t handleIdle();
    int32_t handlePulseBoot();
    int32_t handlePulseScan();
    int32_t handlePulseSleep();
    int32_t handlePoweringOff();

    /* Transition to a new state */
    void setState(RFIDState newState);

    /* End session and power off */
    void endSession(const char *reason);
};

extern RFIDModule *rfidModule;
