#!/usr/bin/env python3
import os
import sys
import subprocess
import time
import shutil

def run_cmd(cmd, ignore_error=False):
    result = subprocess.run(cmd, shell=True, text=True, capture_output=True)
    if result.returncode != 0 and not ignore_error:
        print(f"[!] Error: {result.stderr.strip()}")
    return result.stdout.strip()

def main():
    if os.geteuid() != 0:
        print("[!] Must run as root.")
        sys.exit(1)

    # 1. Ensure PermitTunnel is enabled
    sshd_config = "/etc/ssh/sshd_config"
    with open(sshd_config, "r") as f:
        if "PermitTunnel yes" not in f.read():
            print("[*] Enabling PermitTunnel in sshd_config...")
            with open(sshd_config, "a") as f_append:
                f_append.write("\nPermitTunnel yes\n")
            
            # Restart whichever SSH service exists
            print("[*] Restarting SSH service...")
            run_cmd("systemctl restart ssh || systemctl restart sshd")
            print("[!] SSH service restarted. Please reconnect your SSH session and run this script again.")
            sys.exit(0)

    print("[*] Waiting for client to establish tunnel (tun0)...")
    while run_cmd("ip link show tun0", ignore_error=True) == "":
        time.sleep(1)

    # 2. Backup Current State
    print("[*] Backing up current network state...")
    orig_default_route = run_cmd("ip route show default")
    has_orig_route = bool(orig_default_route)
    shutil.copy2("/etc/resolv.conf", "/tmp/resolv.conf.bak")
    
    ssh_client_env = os.environ.get("SSH_CLIENT", "")
    client_ip = ssh_client_env.split()[0] if ssh_client_env else ""
    gateway_ip = orig_default_route.split()[2] if has_orig_route and "via" in orig_default_route else ""

    # 3. Create Independent Watchdog
    watchdog_sh = f"""#!/bin/bash
    while ip link show tun0 >/dev/null 2>&1 && kill -0 {os.getpid()} 2>/dev/null; do
        sleep 2
    done
    
    ip route del default 2>/dev/null
    ip route del 10.0.0.0/8 2>/dev/null
    ip route del 172.16.0.0/12 2>/dev/null
    ip route del 192.168.0.0/16 2>/dev/null
    
    if [ "{has_orig_route}" = "True" ]; then
        ip route add {orig_default_route} 2>/dev/null
    fi
    
    if [ -n "{client_ip}" ] && [ -n "{gateway_ip}" ]; then
        ip route del {client_ip} via {gateway_ip} 2>/dev/null
    fi
    
    [ -f /tmp/resolv.conf.bak ] && cat /tmp/resolv.conf.bak > /etc/resolv.conf
    rm /tmp/resolv.conf.bak /tmp/net_restore.sh 2>/dev/null
    """
    
    with open("/tmp/net_restore.sh", "w") as f:
        f.write(watchdog_sh)
    os.chmod("/tmp/net_restore.sh", 0o755)

    subprocess.Popen(["nohup", "/tmp/net_restore.sh"], 
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, 
                     preexec_fn=os.setpgrp)

    # 4. Configure Tunnel and Local Pools
    print("[*] Configuring tun0, local IP pools, and routing...")
    run_cmd("ip addr add 10.8.0.2 peer 10.8.0.1 dev tun0", ignore_error=True)
    run_cmd("ip link set tun0 up")

    if client_ip and gateway_ip:
        run_cmd(f"ip route add {client_ip} via {gateway_ip}", ignore_error=True)

    if gateway_ip:
        print("[*] Bypassing local IP pools (RFC1918) from tunnel...")
        run_cmd(f"ip route replace 10.0.0.0/8 via {gateway_ip}", ignore_error=True)
        run_cmd(f"ip route replace 172.16.0.0/12 via {gateway_ip}", ignore_error=True)
        run_cmd(f"ip route replace 192.168.0.0/16 via {gateway_ip}", ignore_error=True)

    if has_orig_route:
        run_cmd("ip route del default", ignore_error=True)
    
    run_cmd("ip route add default via 10.8.0.1")

    with open("/etc/resolv.conf", "w") as f:
        f.write("nameserver 8.8.8.8\n")

    print("\n[+] Tunnel active. Internet traffic routed through 10.8.0.1.")
    print("[+] Local subnets remain on original gateway.")
    print("[*] Watchdog active. Network will restore automatically if connection drops or script is killed.")

    # 5. Monitor Loop
    try:
        while run_cmd("ip link show tun0", ignore_error=True) != "":
            time.sleep(2)
        print("\n[!] tun0 interface lost. Watchdog is restoring network...")
    except KeyboardInterrupt:
        print("\n[*] Script interrupted. Watchdog will clean up.")

if __name__ == "__main__":
    main()