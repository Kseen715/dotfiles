#!/usr/bin/env python3
import os
import sys
import subprocess
import time
import argparse

def run_cmd(cmd, ignore_error=False):
    result = subprocess.run(cmd, shell=True, capture_output=True)
    return result.stdout.strip()

def main():
    if os.geteuid() != 0:
        print("[!] Must run as root (sudo).")
        sys.exit(1)

    parser = argparse.ArgumentParser()
    parser.add_argument("server_ip")
    parser.add_argument("interface")
    args = parser.parse_args()

    print(f"[*] Establishing SSH tunnel to {args.server_ip}...")
    ssh_proc = subprocess.Popen(["ssh", "-w", "0:0", "-N", f"root@{args.server_ip}"])

    try:
        while run_cmd("ip link show tun0", ignore_error=True) == b"":
            if ssh_proc.poll() is not None:
                print("[!] SSH connection failed or terminated.")
                sys.exit(1)
            time.sleep(1)

        print("[*] Configuring local tun0 and NAT...")
        run_cmd("ip addr add 10.8.0.1 peer 10.8.0.2 dev tun0")
        run_cmd("ip link set tun0 up")
        run_cmd("sysctl -w net.ipv4.ip_forward=1")
        run_cmd(f"iptables -t nat -A POSTROUTING -o {args.interface} -j MASQUERADE")

        print("\n[+] Client tunnel active. Providing internet to server.")
        print("[*] Press Ctrl+C to close tunnel and clean up iptables.")
        ssh_proc.wait()

    except KeyboardInterrupt:
        print("\n[*] Stopping tunnel...")

    finally:
        print("[*] Cleaning up client state...")
        run_cmd(f"iptables -t nat -D POSTROUTING -o {args.interface} -j MASQUERADE", ignore_error=True)
        run_cmd("sysctl -w net.ipv4.ip_forward=0", ignore_error=True)
        if ssh_proc.poll() is None:
            ssh_proc.terminate()
        print("[+] Cleanup complete.")

if __name__ == "__main__":
    main()