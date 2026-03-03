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
PKT_STRUCT = 3
PKT_FILE_OFFER = 4
PKT_FILE_CHUNK = 5
PKT_FILE_END = 6

def setup_node(name, peer_id, role):
    path = f"test_env/{name}"
    os.makedirs(f"{path}/configs", exist_ok=True)
    os.makedirs(f"{path}/media", exist_ok=True)
    with open(f"{path}/configs/identity.json", "w") as f:
        f.write(f'{{"identity":{{"id":"{peer_id}"}},"role":{role},"fullscreen":false}}')
    with open(f"{path}/configs/warps.json", "w") as f:
        f.write('{"peers":{}}')
    
    # Each node gets its own working directory
    work_dir = f"test_env/work_{name}"
    os.makedirs(f"{work_dir}/data", exist_ok=True)
    with open(f"{work_dir}/data/settings.json", "w") as f:
        f.write(f'{{"projectPath":"{os.path.abspath(path)}"}}')
    
    return os.path.abspath(path), os.path.abspath(work_dir)

def build_header(p_type, sender_id="MASTER01"):
    # senderId is 9 bytes (8 chars + null)
    sid = sender_id.encode('ascii')[:8].ljust(9, b'\x00')
    return struct.pack("BB9s", PACKET_ID, p_type, sid)

def test_protocol_driver():
    print("--- Starting Protocol Driver Integration Test ---")
    if os.path.exists("test_env"):
        shutil.rmtree("test_env")
    
    test_env_vars = os.environ.copy()
    test_env_vars["INVASIV_TEST_ADDR"] = "127.0.0.1"
    test_env_vars["NO_AT_BRIDGE"] = "1"
    
    # Setup node
    path_node, work_node = setup_node("peer", "PEER01", 0)
    peer_warp_file = os.path.join(path_node, "configs/warps.json")
    peer_media_file = os.path.join(path_node, "media/sync_test.txt")

    bin_path = os.path.abspath("./bin/invasiv")

    # Setup Master Sockets
    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try: listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except: pass
    listen_sock.bind(('127.0.0.1', 9000))
    listen_sock.settimeout(15.0)

    send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    process = None
    log_files = []
    try:
        print(f"Launching Invasiv Peer in {work_node}...")
        f_app = open(f"test_env/app.log", "w")
        log_files.append(f_app)
        # Use --headless flag and run from work_node
        process = subprocess.Popen([bin_path, "--headless"], cwd=work_node, stdout=f_app, stderr=subprocess.STDOUT, 
                                 env=test_env_vars, preexec_fn=os.setsid)
        
        # 1. TEST: Heartbeat Reception
        print("Waiting for heartbeat...")
        start_time = time.time()
        heartbeat_found = False
        while time.time() - start_time < 25:
            try:
                data, addr = listen_sock.recvfrom(1024)
                if len(data) < 11: continue
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID and p_type == PKT_HEARTBEAT:
                    # peerId is at offset 11 in HeartbeatPacket
                    peer_id = data[11:19].decode('ascii').strip('\x00')
                    print(f"SUCCESS: Received heartbeat from {peer_id} at {addr}")
                    heartbeat_found = True
                    break
            except socket.timeout:
                continue
        
        if not heartbeat_found:
            print("FAILED: App did not broadcast heartbeat.")
            return False

        # 2. TEST: Structure Propagation
        print("Testing Structure Sync...")
        test_warp = {
            "peers": {
                "MASTER01": [
                    {"id": "SURF_01", "ownerId": "MASTER01", "contentId": "test.mp4", "rows": 1, "cols": 1}
                ]
            }
        }
        j_str = json.dumps(test_warp)
        payload = build_header(PKT_STRUCT) + j_str.encode('utf-8')
        send_sock.sendto(payload, ('127.0.0.1', 9000))
        
        print("Polling disk for warps.json update...")
        start_time = time.time()
        synced = False
        while time.time() - start_time < 10:
            if os.path.exists(peer_warp_file):
                with open(peer_warp_file, 'r') as f:
                    try:
                        data = json.load(f)
                        if "peers" in data and "MASTER01" in data["peers"]:
                            print("SUCCESS: warps.json updated via network.")
                            synced = True
                            break
                    except: pass
            time.sleep(0.5)
        if not synced:
            print("FAILED: Structure did not propagate to disk.")
            return False

        # 3. TEST: File Transfer
        print("Testing File Synchronization...")
        filename = "sync_test.txt"
        file_content = b"distributed_visual_logic_2026"
        
        offer = build_header(PKT_FILE_OFFER) + struct.pack("IH33s", len(file_content), len(filename), b"dummy_hash") + filename.encode('ascii')
        send_sock.sendto(offer, ('127.0.0.1', 9000))
        time.sleep(1.0)
        
        chunk = build_header(PKT_FILE_CHUNK) + struct.pack("IH", 0, len(file_content)) + file_content
        send_sock.sendto(chunk, ('127.0.0.1', 9000))
        time.sleep(1.0)
        
        end = build_header(PKT_FILE_END)
        send_sock.sendto(end, ('127.0.0.1', 9000))
        
        print("Polling disk for media sync...")
        start_time = time.time()
        file_arrived = False
        while time.time() - start_time < 15:
            if os.path.exists(peer_media_file):
                with open(peer_media_file, "rb") as f:
                    if f.read() == file_content:
                        print("SUCCESS: File arrived and verified.")
                        file_arrived = True
                        break
            time.sleep(0.5)
        
        if not file_arrived:
            print("FAILED: File did not synchronize.")
            return False

        print("--- ALL PROTOCOL TESTS PASSED ---")
        return True

    except Exception as e:
        print(f"EXCEPTION: {e}")
    finally:
        listen_sock.close()
        send_sock.close()
        if process:
            try: os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            except: pass
        for f in log_files:
            f.close()
        if os.path.exists("test_env") and False:
            shutil.rmtree("test_env")

    return False

if __name__ == "__main__":
    if test_protocol_driver():
        exit(0)
    else:
        if os.path.exists("test_env/app.log"):
            with open("test_env/app.log", "r") as f:
                print("--- APP LOGS ---")
                print(f.read())
        exit(1)
