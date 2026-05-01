import socket
import struct
import time
import random
import select
import sys

# Packet Types
C_CONNECT = 1
S_MATCH_START = 2
C_LOCK_PIECE = 3
S_GRANT_PIECE = 4
C_STATE_UPDATE = 5
S_STATE_BROADCAST = 6
S_WEAK_CONNECTION = 7

# Packet Structures (Little Endian)
HEADER_FMT = "<BBI"
COMMAND_FMT = HEADER_FMT + "H"

class TetrisServer:
    def __init__(self, host='0.0.0.0', port=12345):
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((host, port))
        self.server_sock.listen(5)
        self.clients = []
        self.seed = random.randint(0, 65535)

    def run(self):
        print(f"DEBUG SERVER started on port 12345, seed: {self.seed}", flush=True)
        print("Waiting for exactly 2 connections...", flush=True)
        
        inputs = [self.server_sock]
        
        while True:
            readable, _, _ = select.select(inputs, [], [], 0.1)
            
            for s in readable:
                if s is self.server_sock:
                    conn, addr = s.accept()
                    print(f"CONNECTION: New client from {addr}", flush=True)
                    self.clients.append(conn)
                    inputs.append(conn)
                    
                    client_count = len(self.clients)
                    print(f"STATUS: {client_count} clients connected", flush=True)
                    
                    if client_count == 2:
                        print("MATCH: 2 clients reached, sending START signal to both...", flush=True)
                        for i, c in enumerate(self.clients):
                            cid = i + 1
                            pkt = struct.pack(COMMAND_FMT, S_MATCH_START, cid, 0, self.seed)
                            c.sendall(pkt)
                else:
                    try:
                        data = s.recv(1024)
                        if not data:
                            print("DISCONNECT: Client closed connection gracefully", flush=True)
                            inputs.remove(s)
                            if s in self.clients: self.clients.remove(s)
                            continue
                        
                        # Just log activity for now
                        cid = self.clients.index(s) + 1 if s in self.clients else 0
                        if data[0] == C_LOCK_PIECE:
                            print(f"ACTIVITY: Client {cid} sent LOCK request", flush=True)
                            # Grant index 0 always for debug
                            grant_pkt = struct.pack(COMMAND_FMT, S_GRANT_PIECE, cid, 0, 0)
                            s.sendall(grant_pkt)
                            
                    except Exception as e:
                        print(f"ERROR: {e}", flush=True)
                        inputs.remove(s)
                        if s in self.clients: self.clients.remove(s)

if __name__ == "__main__":
    server = TetrisServer()
    server.run()
