#!/usr/bin/env python3
"""
RFID Meshtastic Client

Session-based RFID reader control over mesh. Sends START/STOP commands
to the reader node and displays continuous heartbeat stream.

Usage:
    uv run python rfid_client.py                     # Listen only
    uv run python rfid_client.py start               # Start reading session
    uv run python rfid_client.py stop                 # Stop reading session
    uv run python rfid_client.py poweron              # Force RFID reader ON (debug)
    uv run python rfid_client.py poweroff             # Force RFID reader OFF (debug)
    uv run python rfid_client.py start --dest !d9a0f6a2  # Send to specific node
    uv run python rfid_client.py --port /dev/tty.usbmodem1234  # Specify serial port
"""

import argparse
import sys
import time
from datetime import datetime

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
        ts = datetime.now().strftime("%H:%M:%S")

        if payload.startswith("RFID:"):
            data = payload[5:]
            if data == "NOTAG":
                print(f"[{ts}] [{from_id}] Session timeout - no tag found (RSSI: {rssi}, SNR: {snr})")
            elif data == "GONE":
                print(f"[{ts}] [{from_id}] Tag left range - session ended (RSSI: {rssi}, SNR: {snr})")
            else:
                print(f"[{ts}] [{from_id}] Tag: {data} (RSSI: {rssi}, SNR: {snr})")
        elif payload in ("START", "STOP"):
            print(f"[{ts}] [{from_id}] {payload} command (RSSI: {rssi}, SNR: {snr})")
        else:
            print(f"[{ts}] [{from_id}] Unknown: {payload} (RSSI: {rssi}, SNR: {snr})")


def on_connection(interface, topic=pub.AUTO_TOPIC):
    print(f"Connected to {interface.myInfo.my_node_num}")


def main():
    parser = argparse.ArgumentParser(description="RFID Meshtastic Client")
    parser.add_argument("command", nargs="?", default="listen",
                        choices=["start", "stop", "listen", "poweron", "poweroff"],
                        help="'start' to begin session, 'stop' to end, 'listen' to just listen (default)")
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

    if args.command in ("start", "stop", "poweron", "poweroff"):
        cmd = {
            "start": "START",
            "stop": "STOP",
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
