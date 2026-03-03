import socket
import struct
import subprocess
import time
import os
import signal
import sys

# Constants from PacketDef.h
PACKET_ID = 0xAA
PKT_HEARTBEAT = 1

def test_heartbeat_reception():
    # Setup socket to listen for broadcast heartbeats
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(1.0) 
    sock.bind(('', 9000))

    print("--- Starting Invasiv Smoke Test ---")
    
    os.makedirs("test_project/configs", exist_ok=True)
    with open("test_project/configs/identity.json", "w") as f:
        f.write('{"identity":{"id":"TESTER01"},"fullscreen":false}')
    
    with open("settings.json", "w") as f:
        f.write('{"projectPath":"' + os.path.abspath("test_project") + '"}')

    bin_path = "./bin/invasiv"
    if not os.path.exists(bin_path):
        print(f"Error: Binary {bin_path} not found.")
        return False

    process = None
    try:
        # Launch with stdout/stderr going to pipe
        # We don't need xvfb-run here because the parent script is already run under it
        process = subprocess.Popen([bin_path], 
                                  stdout=subprocess.PIPE, 
                                  stderr=subprocess.STDOUT,
                                  text=True,
                                  preexec_fn=os.setsid)
        
        print(f"Invasiv launched with PID {process.pid}. Waiting for heartbeat...")
        
        start_time = time.time()
        while time.time() - start_time < 15:
            # Check if process is still running
            if process.poll() is not None:
                print("ERROR: Invasiv exited prematurely.")
                print("--- LOG START ---")
                print(process.stdout.read())
                print("--- LOG END ---")
                return False

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
                continue
                
        print("TIMEOUT: No heartbeat received within 15s.")
        print("--- LAST LOGS ---")
        # Try to read whatever is in the pipe
        import fcntl
        fd = process.stdout.fileno()
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
        try:
            print(process.stdout.read())
        except:
            pass
            
    except Exception as e:
        print(f"Error during test: {e}")
    finally:
        if process:
            try:
                os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            except:
                pass
            print("Invasiv process terminated.")

    return False

if __name__ == "__main__":
    if test_heartbeat_reception():
        print("Integration Test PASSED")
        exit(0)
    else:
        print("Integration Test FAILED")
        exit(1)
