import socket
import struct
import time
import random
import select
import sys
import ctypes
from protocol import *

class TetrisServer:
    def __init__(self, host='0.0.0.0', port=12345):
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((host, port))
        self.server_sock.listen(5)
        self.clients = []
        self.client_buffers = {} # socket -> bytearray
        self.client_types = {} # socket -> char
        self.seed = random.randint(0, 65535)
        self.next_piece_index = 0
        self.countdown = -1
        self.last_tick = 0

    def send_command(self, sock, p_type, client_id, sequence, data):
        pkt = CommandPacket()
        pkt.header.type = p_type
        pkt.header.client_id = client_id
        pkt.header.sequence = sequence
        pkt.data = data
        size_hdr = struct.pack(">H", ctypes.sizeof(pkt))
        print(f"[NET SEND] -> Client {client_id}: {PKT_NAMES.get(p_type, 'UNKNOWN')} (len: {ctypes.sizeof(pkt)}, data: {data})", flush=True)
        sock.sendall(size_hdr + bytes(pkt))

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
                            self.send_command(c, S_COUNTDOWN, i+1, 0, self.countdown)
                        self.countdown -= 1
                        self.last_tick = now
                    else:
                        print("MATCH: Countdown finished, sending START signal...", flush=True)
                        for i, c in enumerate(self.clients):
                            self.send_command(c, S_MATCH_START, i+1, 0, self.seed)
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
                            self.client_types = {}
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
                            pkt_len = struct.unpack_from(">H", buf, 0)[0]
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

        if p_type == C_SET_CHARACTER:
            if len(data) >= ctypes.sizeof(CommandPacket):
                pkt = CommandPacket.from_buffer_copy(data)
                client_char = pkt.data
                self.client_types[s] = client_char
                print(f"CHARACTER: Client {cid} registered as character {client_char}", flush=True)

        elif p_type == C_LOCK_PIECE:
            granted = self.next_piece_index
            self.next_piece_index += 1
            print(f"ACTIVITY: Client {cid} LOCK -> Index {granted}. Next preview: {self.next_piece_index}", flush=True)
            self.send_command(s, S_GRANT_PIECE, cid, 0, granted)
            # Broadcast the NEW next piece index to everyone
            for c in self.clients:
                self.send_command(c, S_NEXT_PIECE_UPDATE, 0, 0, self.next_piece_index)
        elif p_type == C_GAME_OVER:
            print(f"GAME OVER: Client {cid} lost the game!", flush=True)
            for c in self.clients:
                self.send_command(c, S_GAME_OVER, cid, 0, 0)
        elif p_type == C_SEND_GARBAGE:
            if len(data) >= ctypes.sizeof(CommandPacket):
                pkt = CommandPacket.from_buffer_copy(data)
                lines = pkt.data
                print(f"GARBAGE: Client {cid} sending {lines} lines to opponent!", flush=True)
                for c in self.clients:
                    if c is not s:
                        self.send_command(c, S_GARBAGE_SIGNAL, cid, 0, lines)
        elif p_type == C_REQUEST_POWER:
            if len(data) >= ctypes.sizeof(CommandPacket):
                pkt = CommandPacket.from_buffer_copy(data)
                crystals = pkt.data
                client_char = self.client_types.get(s, 1)
                print(f"POWER: Client {cid} (Char {client_char}) requesting power with {crystals} crystals!", flush=True)
                
                if client_char == 1:
                    power_id = 1 if crystals <= 2 else 2
                elif client_char == 2:
                    power_id = 2 if crystals <= 2 else 1
                else:
                    power_id = crystals if crystals <= 2 else 2
                    
                if power_id > 0:
                    print(f"POWER: Server authorizing power {power_id} for Client {cid}", flush=True)
                    for c in self.clients:
                        self.send_command(c, S_POWER_ACTIVATED, cid, 0, power_id)
        elif p_type == C_STATE_UPDATE:
            # Broadcast to other client
            if len(data) >= ctypes.sizeof(StateUpdatePacket):
                # Update header type to broadcast
                pkt = StateUpdatePacket.from_buffer_copy(data)
                pkt.header.type = S_STATE_BROADCAST
                header = struct.pack(">H", ctypes.sizeof(pkt))
                for c in self.clients:
                    if c is not s:
                        c.sendall(header + bytes(pkt))

if __name__ == "__main__":
    server = TetrisServer()
    server.run()
