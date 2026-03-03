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

def setup_node():
    path = "test_env/node"
    os.makedirs(f"{path}/configs", exist_ok=True)
    os.makedirs(f"{path}/media", exist_ok=True)
    with open(f"{path}/configs/identity.json", "w") as f:
        f.write('{"identity":{"id":"TEST_PEER"},"role":0,"fullscreen":false}')
    with open("settings.json", "w") as f:
        f.write(f'{{"projectPath":"{os.path.abspath(path)}"}}')
    return os.path.abspath(path)

def build_header(p_type):
    return struct.pack("BB9s", PACKET_ID, p_type, b"MASTER01")

def test_protocol_driver():
    print("--- Starting Protocol Driver Integration Test ---")
    if os.path.exists("test_env"):
        shutil.rmtree("test_env")
    
    node_path = setup_node()
    peer_warp_file = os.path.join(node_path, "configs/warps.json")
    peer_media_file = os.path.join(node_path, "media/sync_test.txt")

    bin_path = os.path.abspath("./bin/invasiv")
    
    # Environment for deterministic network
    test_env = os.environ.copy()
    test_env["INVASIV_TEST_ADDR"] = "127.0.0.1"
    test_env["NO_AT_BRIDGE"] = "1"

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
    try:
        print("Launching Invasiv Peer in headless mode...")
        f_app = open(f"test_env/app.log", "w")
        process = subprocess.Popen([bin_path, "--headless"], stdout=f_app, stderr=subprocess.STDOUT, 
                                 env=test_env, preexec_fn=os.setsid)
        
        # 1. TEST: Heartbeat Reception
        print("Waiting for heartbeat...")
        start_time = time.time()
        heartbeat_found = False
        while time.time() - start_time < 25:
            try:
                data, addr = listen_sock.recvfrom(1024)
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID and p_type == PKT_HEARTBEAT:
                    print(f"SUCCESS: Received heartbeat from {addr}")
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
