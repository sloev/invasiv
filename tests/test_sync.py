import json
import socket
import struct
import subprocess
import time
import os
import signal
import shutil
import sys

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
    os.makedirs(work_dir, exist_ok=True)
    # Write settings.json to CWD
    with open(f"{work_dir}/settings.json", "w") as f:
        f.write(f'{{"projectPath":"{os.path.abspath(path)}"}}')
    
    return os.path.abspath(path), os.path.abspath(work_dir)

def build_header(p_type, sender_id="MASTER01"):
    # senderId is 9 bytes (8 chars + null)
    sid = sender_id.encode('ascii')[:8].ljust(9, b'\x00')
    return struct.pack("BB9s", PACKET_ID, p_type, sid)

def test_protocol_driver():
    print("--- Starting Pure CLI Protocol Driver Test ---")
    if os.path.exists("test_env"):
        shutil.rmtree("test_env")
    
    test_env_vars = os.environ.copy()
    # Use 127.0.0.1 for maximum reliability in isolated CI/Docker environments
    test_env_vars["INVASIV_TEST_ADDR"] = "127.0.0.1"
    
    # Setup node
    path_node, work_node = setup_node("peer", "PEER01", 0)
    peer_warp_file = os.path.join(path_node, "configs/warps.json")
    peer_media_file = os.path.join(path_node, "media/sync_test.txt")

    bin_path = os.path.abspath("./bin/invasiv")

    # Setup sockets
    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind(('127.0.0.1', 9000))
    listen_sock.settimeout(1.0)

    send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    process = None
    try:
        print(f"Launching Invasiv in Headless CLI mode...")
        # Capture stdout/stderr to debug app crashes
        process = subprocess.Popen(
            [bin_path, "--headless"], 
            cwd=work_node, 
            env=test_env_vars, 
            preexec_fn=os.setsid,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # 1. TEST: Heartbeat
        print("Waiting for heartbeat (up to 30s)...")
        heartbeat_found = False
        start_time = time.time()
        while time.time() - start_time < 30:
            # Check if process is still alive
            if process.poll() is not None:
                print(f"FAILED: Invasiv process exited prematurely with code {process.returncode}")
                stdout, stderr = process.communicate()
                print(f"STDOUT: {stdout}\nSTDERR: {stderr}")
                return False

            try:
                data, addr = listen_sock.recvfrom(1024)
                if len(data) < 11: continue
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID and p_type == PKT_HEARTBEAT:
                    peer_id = data[11:19].decode('ascii').strip('\x00')
                    print(f"SUCCESS: Heartbeat from {peer_id} @ {addr}")
                    heartbeat_found = True
                    break
            except socket.timeout: 
                print(".", end="", flush=True)
                continue
        
        print("")
        if not heartbeat_found:
            print("FAILED: No heartbeat broadcast. Checking app logs...")
            # We can't use communicate() here because it blocks, but we can kill and read
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            stdout, stderr = process.communicate()
            print(f"APP STDOUT:\n{stdout}")
            print(f"APP STDERR:\n{stderr}")
            process = None
            return False

        # 2. TEST: Structure Sync
        print("Injecting Structure Sync...")
        test_warp = {
            "peers": { "MASTER01": [{"id": "S1", "ownerId": "MASTER01", "contentId": "t.mp4", "rows": 1, "cols": 1}] }
        }
        payload = build_header(PKT_STRUCT) + json.dumps(test_warp).encode('utf-8')
        send_sock.sendto(payload, ('127.0.0.1', 9000))
        
        # Poll for disk write
        print("Verifying persistence on disk...")
        synced = False
        start_time = time.time()
        while time.time() - start_time < 10:
            if os.path.exists(peer_warp_file):
                with open(peer_warp_file, 'r') as f:
                    try:
                        data = json.load(f)
                        if "peers" in data and "MASTER01" in data["peers"]:
                            print("SUCCESS: warps.json updated.")
                            synced = True
                            break
                    except: pass
            time.sleep(0.5)
        if not synced:
            print("FAILED: Disk not updated.")
            return False

        # 3. TEST: File Sync
        print("Injecting File Transfer...")
        filename = "sync_test.txt"
        content = b"cli_test_2026"
        
        # Offer
        offer = build_header(PKT_FILE_OFFER) + struct.pack("IH33s", len(content), len(filename), b"h") + filename.encode('ascii')
        send_sock.sendto(offer, ('127.0.0.1', 9000))
        time.sleep(0.2)
        
        # Chunk
        chunk = build_header(PKT_FILE_CHUNK) + struct.pack("IH", 0, len(content)) + content
        send_sock.sendto(chunk, ('127.0.0.1', 9000))
        time.sleep(0.2)
        
        # End
        send_sock.sendto(build_header(PKT_FILE_END), ('127.0.0.1', 9000))
        
        print("Verifying file arrival...")
        arrived = False
        start_time = time.time()
        while time.time() - start_time < 10:
            if os.path.exists(peer_media_file):
                with open(peer_media_file, "rb") as f:
                    if f.read() == content:
                        print("SUCCESS: File synced.")
                        arrived = True
                        break
            time.sleep(0.5)
        
        if not arrived:
            print("FAILED: File sync failed.")
            return False

        print("--- PURE CLI INTEGRATION PASSED ---")
        return True

    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        if process:
            try: 
                os.killpg(os.getpgid(process.pid), signal.SIGTERM)
                stdout, stderr = process.communicate(timeout=2)
                if stdout or stderr:
                    print(f"--- APP LOGS ---\n{stdout}\n{stderr}")
            except: pass
        if os.path.exists("test_env"):
            shutil.rmtree("test_env")
    return False

if __name__ == "__main__":
    if test_protocol_driver(): exit(0)
    else: exit(1)
