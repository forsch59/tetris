import socket
import struct
import time
import random
import select
import sys

# Packet Types
PKT_NAMES = {
    1: "C_CONNECT",
    2: "S_MATCH_START",
    3: "C_LOCK_PIECE",
    4: "S_GRANT_PIECE",
    5: "C_STATE_UPDATE",
    6: "S_STATE_BROADCAST",
    7: "S_WEAK_CONNECTION",
    8: "S_COUNTDOWN",
    9: "S_NEXT_PIECE_UPDATE",
    10: "C_GAME_OVER",
    11: "S_GAME_OVER",
    12: "C_ACTIVATE_POWERUP",
    13: "S_POWERUP_SIGNAL"
}

C_CONNECT = 1
S_MATCH_START = 2
C_LOCK_PIECE = 3
S_GRANT_PIECE = 4
C_STATE_UPDATE = 5
S_STATE_BROADCAST = 6
S_WEAK_CONNECTION = 7
S_COUNTDOWN = 8
S_NEXT_PIECE_UPDATE = 9
C_GAME_OVER = 10
S_GAME_OVER = 11
C_ACTIVATE_POWERUP = 12
S_POWERUP_SIGNAL = 13

# Packet Structures (Little Endian)
# All packets are prefixed with a uint16 length (excluding the length field itself)
HEADER_FMT = "<BBI" # Type (1), ClientID (1), Seq (4) = 6 bytes
COMMAND_FMT = HEADER_FMT + "H" # 8 bytes total
STATE_UPDATE_FMT = HEADER_FMT + "bbbbH100s" # 6 + 1+1+1+1 + 2 + 100 = 112 bytes total

class TetrisServer:
    def __init__(self, host='0.0.0.0', port=12345):
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((host, port))
        self.server_sock.listen(5)
        self.clients = []
        self.client_buffers = {} # socket -> bytearray
        self.seed = random.randint(0, 65535)
        self.next_piece_index = 0
        self.countdown = -1
        self.last_tick = 0

    def send_packet(self, sock, fmt, *args):
        payload = struct.pack(fmt, *args)
        header = struct.pack("<H", len(payload))
        p_type = payload[0]
        cid = self.clients.index(sock) + 1 if sock in self.clients else 0
        print(f"[NET SEND] -> Client {cid}: {PKT_NAMES.get(p_type, 'UNKNOWN')} (len: {len(payload)}, data: {args[3] if len(args) > 3 else ''})", flush=True)
        sock.sendall(header + payload)

    def run(self):
        print(f"DEBUG SERVER started on port 12345, seed: {self.seed}", flush=True)
        print("Waiting for exactly 2 connections...", flush=True)
        
        inputs = [self.server_sock]
        
        while True:
            readable, _, _ = select.select(inputs, [], [], 0.1)
            
            # Handle countdown timer
            if self.countdown >= 0:
                now = time.time()
                if now - self.last_tick >= 1.0:
                    if self.countdown > 0:
                        print(f"COUNTDOWN: {self.countdown}...", flush=True)
                        for i, c in enumerate(self.clients):
                            self.send_packet(c, COMMAND_FMT, S_COUNTDOWN, i+1, 0, self.countdown)
                        self.countdown -= 1
                        self.last_tick = now
                    else:
                        print("MATCH: Countdown finished, sending START signal...", flush=True)
                        for i, c in enumerate(self.clients):
                            self.send_packet(c, COMMAND_FMT, S_MATCH_START, i+1, 0, self.seed)
                        self.countdown = -1

            for s in readable:
                if s is self.server_sock:
                    conn, addr = s.accept()
                    print(f"CONNECTION: New client from {addr}", flush=True)
                    self.clients.append(conn)
                    inputs.append(conn)
                    
                    client_count = len(self.clients)
                    print(f"STATUS: {client_count} clients connected", flush=True)
                    
                    if client_count == 2:
                        print("MATCH: 2 clients reached, starting countdown...", flush=True)
                        inputs.remove(self.server_sock)
                        self.countdown = 2 # 2 second timer as requested
                        self.last_tick = 0 # trigger immediately
                else:
                    try:
                        chunk = s.recv(4096)
                        if not chunk:
                            print("DISCONNECT: Client closed connection gracefully. Resetting server.", flush=True)
                            # Reset server state
                            for c in self.clients:
                                try: c.close()
                                except: pass
                            self.clients = []
                            self.client_buffers = {}
                            self.countdown = -1
                            self.next_piece_index = 0
                            self.seed = random.randint(0, 65535)
                            inputs = [self.server_sock]
                            print("Waiting for 2 new connections...", flush=True)
                            break
                        
                        if s not in self.client_buffers:
                            self.client_buffers[s] = bytearray()
                        self.client_buffers[s].extend(chunk)

                        buf = self.client_buffers[s]
                        while len(buf) >= 2:
                            pkt_len = struct.unpack_from("<H", buf, 0)[0]
                            if len(buf) < 2 + pkt_len:
                                break
                            
                            pkt_data = buf[2 : 2 + pkt_len]
                            del buf[: 2 + pkt_len]
                            self.handle_packet(s, pkt_data)
                            
                    except Exception as e:
                        import traceback
                        traceback.print_exc()
                        print(f"ERROR: {e}", flush=True)
                        inputs.remove(s)
                        if s in self.clients: self.clients.remove(s)

    def handle_packet(self, s, data):
        if not data: return
        cid = self.clients.index(s) + 1 if s in self.clients else 0
        p_type = data[0]
        
        # Don't log state updates as they are too frequent
        if p_type != C_STATE_UPDATE:
            print(f"[NET RECV] <- Client {cid}: {PKT_NAMES.get(p_type, 'UNKNOWN')} (len: {len(data)})", flush=True)

        if p_type == C_LOCK_PIECE:
            granted = self.next_piece_index
            self.next_piece_index += 1
            print(f"ACTIVITY: Client {cid} LOCK -> Index {granted}. Next preview: {self.next_piece_index}", flush=True)
            self.send_packet(s, COMMAND_FMT, S_GRANT_PIECE, cid, 0, granted)
            # Broadcast the NEW next piece index to everyone
            for c in self.clients:
                self.send_packet(c, COMMAND_FMT, S_NEXT_PIECE_UPDATE, 0, 0, self.next_piece_index)
        elif p_type == C_GAME_OVER:
            print(f"GAME OVER: Client {cid} lost the game!", flush=True)
            for c in self.clients:
                self.send_packet(c, COMMAND_FMT, S_GAME_OVER, cid, 0, 0)
        elif p_type == C_ACTIVATE_POWERUP:
            print(f"POWERUP: Client {cid} activated a powerup!", flush=True)
            for c in self.clients:
                self.send_packet(c, COMMAND_FMT, S_POWERUP_SIGNAL, cid, 0, 0)
        elif p_type == C_STATE_UPDATE:
            # Broadcast to other client
            if len(data) >= 112:
                # Update header type to broadcast
                broadcast_data = bytearray(data)
                broadcast_data[0] = S_STATE_BROADCAST
                header = struct.pack("<H", len(broadcast_data))
                for c in self.clients:
                    if c is not s:
                        c.sendall(header + broadcast_data)

if __name__ == "__main__":
    server = TetrisServer()
    server.run()
