import socket
import struct
import subprocess
import time
import os
import signal

# Constants from PacketDef.h
PACKET_ID = 0xAA
PKT_HEARTBEAT = 1
PKT_METRONOME = 9

def test_heartbeat_reception():
    # Setup socket to listen for broadcast heartbeats
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(10.0) 
    sock.bind(('', 9000))

    print("--- Starting Invasiv Smoke Test ---")
    
    # We'll mock a "project" by providing a fake configs/warps.json
    # and setting a custom id in identity.json
    os.makedirs("test_project/configs", exist_ok=True)
    with open("test_project/configs/identity.json", "w") as f:
        f.write('{"identity":{"id":"TESTER01"},"fullscreen":false}')
    
    # Launch Invasiv headlessly (using Xvfb)
    # We tell it the project path via command line if possible?
    # Actually, it reads settings.json.
    with open("settings.json", "w") as f:
        f.write('{"projectPath":"' + os.path.abspath("test_project") + '"}')

    # Path to binary (relative to project root after build)
    bin_path = "./bin/invasiv"
    if not os.path.exists(bin_path):
        print(f"Error: Binary {bin_path} not found. Build it first.")
        return False

    process = None
    try:
        # xvfb-run ensures we have a virtual display
        process = subprocess.Popen(["xvfb-run", "-a", bin_path], 
                                  stdout=subprocess.PIPE, 
                                  stderr=subprocess.PIPE,
                                  preexec_fn=os.setsid)
        
        print(f"Invasiv launched with PID {process.pid}. Waiting for heartbeat...")
        
        # We need to wait for at least one heartbeat (sent every 60 frames = 1sec)
        start_time = time.time()
        while time.time() - start_time < 10:
            try:
                data, addr = sock.recvfrom(1024)
                if len(data) < 2: continue
                
                header_id, p_type = struct.unpack_from("BB", data)
                if header_id == PACKET_ID:
                    sender_id = data[2:10].decode('ascii').strip('\x00')
                    print(f"RECEIVED PACKET: Type={p_type} From={sender_id}")
                    
                    if p_type == PKT_HEARTBEAT:
                        print("SUCCESS: Heartbeat received!")
                        return True
            except socket.timeout:
                print("TIMEOUT: No heartbeat received.")
                break
    except Exception as e:
        print(f"Error during test: {e}")
    finally:
        if process:
            # Kill the process group to ensure xvfb dies too
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            print("Invasiv process terminated.")

    return False

if __name__ == "__main__":
    if test_heartbeat_reception():
        print("Integration Test PASSED")
        exit(0)
    else:
        print("Integration Test FAILED")
        exit(1)
