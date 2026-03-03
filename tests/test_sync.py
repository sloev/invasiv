import json
import socket
import struct
import subprocess
import time
import os
import signal
import shutil

# Constants from PacketDef.h
PACKET_ID = 0xAA
PKT_HEARTBEAT = 1

def setup_node(name, peer_id, role):
    path = f"test_env/{name}"
    os.makedirs(f"{path}/configs", exist_ok=True)
    with open(f"{path}/configs/identity.json", "w") as f:
        f.write(f'{{"identity":{{"id":"{peer_id}"}},"role":{role},"fullscreen":false}}')
    # Pre-create empty warps.json
    with open(f"{path}/configs/warps.json", "w") as f:
        f.write('{"peers":{}}')
    return os.path.abspath(path)

def test_master_peer_sync():
    print("--- Starting Multi-Node FS Verification Test ---")
    if os.path.exists("test_env"):
        shutil.rmtree("test_env")
    
    path_peer = setup_node("peer", "PEER01", 0)
    path_master = setup_node("master", "MASTER01", 1)
    peer_warp_file = os.path.join(path_peer, "configs/warps.json")

    bin_path = "./bin/invasiv"
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(1.0)
    sock.bind(('', 9000))

    procs = []
    try:
        # Launch Peer
        with open("settings.json", "w") as f: f.write(f'{{"projectPath":"{path_peer}"}}')
        p_peer = subprocess.Popen([bin_path], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, preexec_fn=os.setsid)
        procs.append(p_peer)
        print("Launched Peer")

        time.sleep(2)

        # Launch Master
        with open("settings.json", "w") as f: f.write(f'{{"projectPath":"{path_master}"}}')
        p_master = subprocess.Popen([bin_path], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, preexec_fn=os.setsid)
        procs.append(p_master)
        print("Launched Master")

        # Wait for handshake
        nodes_seen = set()
        start_time = time.time()
        while len(nodes_seen) < 2 and time.time() - start_time < 15:
            try:
                data, addr = sock.recvfrom(1024)
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID and p_type == PKT_HEARTBEAT:
                    sender_id = data[2:10].decode('ascii').strip('\x00')
                    nodes_seen.add(sender_id)
            except socket.timeout: continue

        if len(nodes_seen) < 2:
            print("FAILED: Discovery timeout.")
            return False

        print("Nodes synced. Triggering 'ADD SURFACE' on Master...")
        subprocess.run(["xdotool", "key", "a"])
        
        # Poll the Peer's warp file for changes
        print(f"Polling {peer_warp_file} for new surface...")
        start_time = time.time()
        while time.time() - start_time < 10:
            if os.path.exists(peer_warp_file):
                with open(peer_warp_file, 'r') as f:
                    try:
                        data = json.load(f)
                        # The master (MASTER01) should have added a layer to itself
                        if "peers" in data and "MASTER01" in data["peers"]:
                            if len(data["peers"]["MASTER01"]) > 0:
                                print("SUCCESS: Master change detected on Peer's disk!")
                                return True
                    except json.JSONDecodeError: pass
            time.sleep(0.5)

        print("FAILED: Peer file never updated with master's changes.")

    except Exception as e:
        print(f"Error: {e}")
    finally:
        for p in procs:
            try: os.killpg(os.getpgid(p.pid), signal.SIGTERM)
            except: pass
        print("Test complete.")

    return False

if __name__ == "__main__":
    if test_master_peer_sync():
        print("Sync Test PASSED")
        exit(0)
    else:
        print("Sync Test FAILED")
        exit(1)
