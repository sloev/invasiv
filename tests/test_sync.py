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
PKT_METRONOME = 9

def setup_node(name, peer_id, role):
    path = f"test_env/{name}"
    os.makedirs(f"{path}/configs", exist_ok=True)
    os.makedirs(f"{path}/media", exist_ok=True)
    with open(f"{path}/configs/identity.json", "w") as f:
        f.write(f'{{"identity":{{"id":"{peer_id}"}},"role":{role},"fullscreen":false}}')
    with open(f"{path}/configs/warps.json", "w") as f:
        f.write('{"peers":{}}')
    return os.path.abspath(path)

def test_full_propagation():
    print("--- Starting Comprehensive Multi-Node Sync Test ---")
    if os.path.exists("test_env"):
        shutil.rmtree("test_env")
    
    path_peer = setup_node("peer", "PEER01", 0)
    path_master = setup_node("master", "MASTER01", 1)
    
    peer_warp_file = os.path.join(path_peer, "configs/warps.json")
    peer_media_file = os.path.join(path_peer, "media/sync_test.txt")
    master_media_file = os.path.join(path_master, "media/sync_test.txt")

    # Determine binary path
    bin_path = "./bin/invasiv"
    if not os.path.exists(bin_path):
        # Try finding it in project structure if not at root
        bin_path = "./bin/invasiv"

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(1.0)
    sock.bind(('', 9000))

    procs = []
    try:
        # 1. Launch instances
        print("Launching Peer...")
        with open("settings.json", "w") as f: f.write(f'{{"projectPath":"{path_peer}"}}')
        p_peer = subprocess.Popen([bin_path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, preexec_fn=os.setsid)
        procs.append(p_peer)
        
        time.sleep(2)
        
        print("Launching Master...")
        with open("settings.json", "w") as f: f.write(f'{{"projectPath":"{path_master}"}}')
        p_master = subprocess.Popen([bin_path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, preexec_fn=os.setsid)
        procs.append(p_master)

        # 2. Verify Handshake
        print("Waiting for node discovery...")
        nodes_seen = set()
        start_time = time.time()
        while len(nodes_seen) < 2 and time.time() - start_time < 20:
            try:
                data, addr = sock.recvfrom(1024)
                if len(data) < 10: continue
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID and p_type == PKT_HEARTBEAT:
                    sender_id = data[2:10].decode('ascii').strip('\x00')
                    nodes_seen.add(sender_id)
                    print(f"Node Online: {sender_id}")
            except socket.timeout: continue

        if len(nodes_seen) < 2:
            print(f"ERROR: Nodes failed to discover each other. Saw: {nodes_seen}")
            # Print master logs for debugging
            print("--- MASTER LOGS ---")
            import fcntl
            fd = p_master.stdout.fileno()
            fl = fcntl.fcntl(fd, fcntl.F_GETFL)
            fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
            print(p_master.stdout.read())
            return False

        print("Handshake Success.")

        # 3. Test Metronome Propagation
        print("Verifying Metronome packets...")
        metro_received = False
        start_time = time.time()
        while time.time() - start_time < 10:
            try:
                data, addr = sock.recvfrom(1024)
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID and p_type == PKT_METRONOME:
                    bpm = struct.unpack_from("f", data, 11)[0]
                    print(f"Success: Received Metronome broadcast ({bpm} BPM)")
                    metro_received = True
                    break
            except socket.timeout: continue
        
        if not metro_received:
            print("ERROR: No Metronome packets detected.")
            return False

        # 4. Test Surface Editing
        print("Triggering Master interaction...")
        subprocess.run(["xdotool", "key", "a"]) 
        
        print("Polling Peer disk for state change...")
        surface_synced = False
        start_time = time.time()
        while time.time() - start_time < 15:
            if os.path.exists(peer_warp_file):
                try:
                    with open(peer_warp_file, 'r') as f:
                        data = json.load(f)
                        if "peers" in data and "MASTER01" in data["peers"]:
                            if len(data["peers"]["MASTER01"]) > 0:
                                print("Success: Master surface configuration persisted on Peer.")
                                surface_synced = True
                                break
                except: pass
            time.sleep(1.0)
        
        if not surface_synced:
            print("ERROR: Surface sync failed to persist on peer disk.")
            return False

        # 5. Test File Sync
        print("Adding file to Master media...")
        with open(master_media_file, "w") as f:
            f.write("test_payload_12345")
        
        print("Waiting for file synchronization...")
        file_synced = False
        start_time = time.time()
        while time.time() - start_time < 20:
            if os.path.exists(peer_media_file):
                with open(peer_media_file, "r") as f:
                    if f.read() == "test_payload_12345":
                        print("Success: Media file synchronized to Peer.")
                        file_synced = True
                        break
            time.sleep(1.0)
        
        if not file_synced:
            print("ERROR: Media file failed to sync.")
            return False

        print("--- COMPREHENSIVE INTEGRATION PASSED ---")
        return True

    except Exception as e:
        print(f"EXCEPTION DURING TEST: {e}")
    finally:
        for p in procs:
            try: os.killpg(os.getpgid(p.pid), signal.SIGTERM)
            except: pass
        if os.path.exists("test_env"):
            shutil.rmtree("test_env")

    return False

if __name__ == "__main__":
    if test_full_propagation():
        exit(0)
    else:
        exit(1)
