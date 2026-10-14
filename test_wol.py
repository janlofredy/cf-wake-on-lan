#!/usr/bin/env python3
import socket
import time
import subprocess
import sys

TARGET_MAC = "00:11:22:33:44:55"
TARGET_IP = "192.168.1.100"
BROADCAST_IP = "192.168.1.255"
PORT = 9

def send_wol(mac_str):
    clean_mac = mac_str.replace(":", "").replace("-", "")
    mac_bytes = bytes.fromhex(clean_mac)
    magic_packet = b"\xff" * 6 + mac_bytes * 16

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.sendto(magic_packet, (BROADCAST_IP, PORT))
        s.sendto(magic_packet, ("255.255.255.255", PORT))
    print(f"🚀 Sent Wake-on-LAN magic packet to {mac_str} ({BROADCAST_IP}:{PORT})")

def is_server_online(ip):
    res = subprocess.run(["ping", "-c", "1", "-W", "1", ip], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return res.returncode == 0

if __name__ == "__main__":
    print(f"🔍 Checking current server status at {TARGET_IP}...")
    if is_server_online(TARGET_IP):
        print(f"⚠️  Server at {TARGET_IP} is CURRENTLY ONLINE!")
        print("   To test WOL, shut down or sleep the server first, then run this script.")
        print()
    else:
        print(f"💤 Server at {TARGET_IP} is currently OFFLINE.")

    send_wol(TARGET_MAC)

    if "--monitor" in sys.argv:
        print(f"⏳ Monitoring {TARGET_IP} for wake-up (Ctrl+C to stop)...")
        start = time.time()
        while True:
            if is_server_online(TARGET_IP):
                elapsed = int(time.time() - start)
                print(f"🎉 SERVER IS ONLINE! Woke up in ~{elapsed} seconds.")
                break
            time.sleep(1)
