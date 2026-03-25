#!/usr/bin/env python3
"""
RFID Meshtastic Client

Sends SCAN commands to the RFID reader node and listens for responses
on PRIVATE_APP port (256).

Usage:
    uv run python rfid_client.py                    # Listen only
    uv run python rfid_client.py scan               # Send SCAN + listen
    uv run python rfid_client.py poweron             # Force RFID reader ON
    uv run python rfid_client.py poweroff            # Force RFID reader OFF
    uv run python rfid_client.py scan --dest !d9a0f6a2  # Send to specific node
    uv run python rfid_client.py --port /dev/tty.usbmodem1234  # Specify serial port
"""

import argparse
import sys
import time

import meshtastic
import meshtastic.serial_interface
from pubsub import pub

PRIVATE_APP_PORT = 256
SENDER_NODE_ID = "!d9a0f6a2"


def on_receive(packet, interface):
    portnum = packet.get("decoded", {}).get("portnum")
    if portnum == "PRIVATE_APP":
        payload = bytes(packet["decoded"]["payload"]).decode("utf-8", errors="replace")
        from_id = packet.get("fromId", "unknown")
        rssi = packet.get("rxRssi", "?")
        snr = packet.get("rxSnr", "?")

        if payload.startswith("RFID:"):
            tag = payload[5:]
            if tag == "NOTAG":
                print(f"[{from_id}] No tag found (RSSI: {rssi}, SNR: {snr})")
            else:
                print(f"[{from_id}] Tag scanned: {tag} (RSSI: {rssi}, SNR: {snr})")
        elif payload == "SCAN":
            print(f"[{from_id}] SCAN request received (RSSI: {rssi}, SNR: {snr})")
        else:
            print(f"[{from_id}] Unknown: {payload} (RSSI: {rssi}, SNR: {snr})")


def on_connection(interface, topic=pub.AUTO_TOPIC):
    print(f"Connected to {interface.myInfo.my_node_num}")


def main():
    parser = argparse.ArgumentParser(description="RFID Meshtastic Client")
    parser.add_argument("command", nargs="?", default="listen",
                        choices=["scan", "listen", "poweron", "poweroff"],
                        help="'scan' to send SCAN, 'poweron'/'poweroff' to toggle RFID power, 'listen' to just listen (default)")
    parser.add_argument("--dest", default=SENDER_NODE_ID,
                        help=f"Destination node ID (default: {SENDER_NODE_ID})")
    parser.add_argument("--port", default=None,
                        help="Serial port (auto-detect if not specified)")
    args = parser.parse_args()

    pub.subscribe(on_receive, "meshtastic.receive")
    pub.subscribe(on_connection, "meshtastic.connection.established")

    print("Connecting to radio...")
    try:
        iface = meshtastic.serial_interface.SerialInterface(devPath=args.port)
    except Exception as e:
        print(f"Failed to connect: {e}")
        sys.exit(1)

    if args.command in ("scan", "poweron", "poweroff"):
        cmd = {
            "scan": "SCAN",
            "poweron": "POWERON",
            "poweroff": "POWEROFF",
        }[args.command]
        print(f"Sending {cmd} to {args.dest}...")
        iface.sendData(
            cmd.encode(),
            portNum=PRIVATE_APP_PORT,
            destinationId=args.dest,
            wantAck=True,
        )
        print(f"{cmd} sent. Waiting for response...")

    print("Listening for RFID messages (Ctrl+C to quit)...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nExiting.")
    finally:
        iface.close()


if __name__ == "__main__":
    main()
