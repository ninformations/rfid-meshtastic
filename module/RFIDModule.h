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

/* Timing configuration (milliseconds) */
#define RFID_POWER_ON_DELAY_MS 200       /* Wait for RDM6300 to boot */
#define RFID_HEARTBEAT_INTERVAL_MS 5000  /* Send tag presence every 5s */
#define RFID_TAG_GONE_TIMEOUT_MS 10000   /* No tag for 10s → gone */
#define RFID_LISTEN_TIMEOUT_MS 30000     /* No tag ever → timeout session */
#define RFID_UART_POLL_MS 50             /* UART check interval */
#define RFID_IDLE_POLL_MS 500            /* Serial check while idle */
#define RFID_POWER_OFF_DELAY_MS 100      /* Brief delay before idle */

/* Serial input buffer for local commands */
#define SERIAL_CMD_BUF_SIZE 32

/* Module states */
enum RFIDState {
    RFID_IDLE,         /* Reader OFF, waiting for START */
    RFID_POWERING_ON,  /* GPIO HIGH, waiting for RDM6300 boot */
    RFID_LISTENING,    /* Reader ON, polling UART for first tag (30s timeout) */
    RFID_READING,      /* Tag in range, heartbeat every 5s, detect tag-gone */
    RFID_POWERING_OFF  /* Shutting down RDM6300 */
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
    uint32_t lastTagSeenAt = 0;      /* millis() of last UART tag read */
    uint32_t lastHeartbeatAt = 0;    /* millis() of last heartbeat sent */
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
    void sendMeshPacket(const char *payload);
    void sendTagHeartbeat(const char *tagHex);
    void sendTagGone();
    void sendNoTagResponse();

    /* ── Local serial command input ── */
    void checkSerialInput();
    void printGPIOStatus();

    /* ── State handlers ── */
    int32_t handleIdle();
    int32_t handlePoweringOn();
    int32_t handleListening();
    int32_t handleReading();
    int32_t handlePoweringOff();

    /* Transition to a new state */
    void setState(RFIDState newState);

    /* End session and power off */
    void endSession(const char *reason);
};

extern RFIDModule *rfidModule;
