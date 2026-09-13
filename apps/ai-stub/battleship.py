"""
Minimal WebSocket stub server for Battleship AI (stdlib only, no pip packages).

Listens on ws://localhost:8001, plays a simple hunt/target AI:
  - Miss  → random unused square
  - Hit   → target adjacent squares (4-way), prefer continuing in same direction

Protocol (JSON, text frames):
  Client sends: {"type":"shot","row":3,"col":5,"result":"hit"|"miss"}
  Server sends: {"type":"shot","row":2,"col":4,"result":"hit"|"miss"}
  Server sends: {"type":"game_over","result":"...","message":"..."}

Usage:  python3 ai-stub/battleship.py
"""
import socket
import hashlib
import struct
import json
import base64
import threading
import random

PORT = 8001
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
BOARD = 10

# ── Ship definitions ───────────────────────────────────────────────
SHIPS = [
    {"name": "Battleship",  "size": 4},
    {"name": "Cruiser",     "size": 3},
    {"name": "Destroyer",   "size": 2},
    {"name": "Submarine",   "size": 2},
    {"name": "Patrol Boat", "size": 1},
]

# ── WebSocket frame helpers (same as chess stub) ───────────────────
def ws_accept_key(key: str) -> str:
    digest = hashlib.sha1((key + GUID).encode()).digest()
    return base64.b64encode(digest).decode()

def build_ws_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    header = bytes([0x80 | opcode, len(payload)])
    return header + payload

def read_ws_frame(sock: socket.socket) -> bytes:
    hdr = sock.recv(2)
    if len(hdr) < 2:
        raise ConnectionError("short header")
    b0, b1 = hdr[0], hdr[1]
    opcode = b0 & 0x0F
    if opcode == 0x8:
        raise ConnectionError("client closed")
    masked = (b1 & 0x80) != 0
    length = b1 & 0x7F
    if length == 126:
        length = struct.unpack("!H", sock.recv(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", sock.recv(8))[0]
    mask = sock.recv(4) if masked else b"\x00" * 4
    payload = bytearray()
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            break
        payload.extend(chunk)
    if masked:
        payload = bytearray(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes(payload)

# ── Board / ship helpers ───────────────────────────────────────────
def make_board() -> list:
    return [[None for _ in range(BOARD)] for _ in range(BOARD)]

def can_place(board, r, c, size, horiz) -> bool:
    for i in range(size):
        dr, dc = (0, i) if horiz else (i, 0)
        rr, cc = r + dr, c + dc
        if rr < 0 or rr >= BOARD or cc < 0 or cc >= BOARD:
            return False
        if board[rr][cc] is not None:
            return False
    return True

def place_ships(board) -> dict:
    """Return a dict shipId -> list of (r, c) cells."""
    ships = {}
    ship_idx = 0
    for ship in SHIPS:
        placed = False
        while not placed:
            r = random.randint(0, BOARD - 1)
            c = random.randint(0, BOARD - 1)
            horiz = random.random() < 0.5
            if can_place(board, r, c, ship["size"], horiz):
                cells = []
                for i in range(ship["size"]):
                    dr, dc = (0, i) if horiz else (i, 0)
                    board[r + dr][c + dc] = ship_idx
                    cells.append((r + dr, c + dc))
                ships[ship_idx] = cells
                placed = True
        ship_idx += 1
    return ships

def flood_destroy(board, ships, start_r, start_c):
    """Remove all cells of the ship at (start_r, start_c). Return ship id."""
    ship_id = board[start_r][start_c]
    visited = set()
    stack = [(start_r, start_c)]
    while stack:
        r, c = stack.pop()
        if (r, c) in visited:
            continue
        visited.add((r, c))
        if r < 0 or r >= BOARD or c < 0 or c >= BOARD:
            continue
        if board[r][c] != ship_id:
            continue
        board[r][c] = None
        for dr, dc in ((1,0),(-1,0),(0,1),(0,-1)):
            stack.append((r + dr, c + dc))
    return ship_id

def all_ships_destroyed(ships: dict, destroyed: set) -> bool:
    return len(destroyed) == len(ships)

# ── AI state (per connection) ──────────────────────────────────────
class AIBattleship:
    def __init__(self):
        self.board = make_board()
        self.ships = place_ships(self.board)
        self.shots = set()          # (r, c) already shot
        self.destroyed = set()      # ship ids sunk
        self.last_hit = None        # (r, c) of last hit, or None
        self.last_dir = None        # (dr, dc) direction to continue, or None

    def choose_shot(self, prev_result: str) -> tuple:
        """Pick a square. prev_result is the result of the player's last shot."""
        if prev_result == "hit":
            return self._target()
        return self._hunt()

    def _hunt(self) -> tuple:
        """Random unused square."""
        while True:
            r = random.randint(0, BOARD - 1)
            c = random.randint(0, BOARD - 1)
            if (r, c) not in self.shots:
                return (r, c)

    def _target(self) -> tuple:
        """Adjacent to last hit; prefer continuing in same direction."""
        lr, lc = self.last_hit
        neighbors = []
        if self.last_dir:
            dr, dc = self.last_dir
            tr, tc = lr + dr, lc + dc
            if 0 <= tr < BOARD and 0 <= tc < BOARD and (tr, tc) not in self.shots:
                neighbors.append((tr, tc))
        for dr, dc in ((1,0),(-1,0),(0,1),(0,-1)):
            tr, tc = lr + dr, lc + dc
            if 0 <= tr < BOARD and 0 <= tc < BOARD and (tr, tc) not in self.shots:
                neighbors.append((tr, tc))
        if not neighbors:
            return self._hunt()
        return random.choice(neighbors)

    def resolve(self, r: int, c: int) -> str:
        """Return "hit" or "miss" and update internal state."""
        self.shots.add((r, c))
        if self.board[r][c] is not None:
            ship_id = flood_destroy(self.board, self.ships, r, c)
            self.destroyed.add(ship_id)
            self.last_hit = (r, c)
            self.last_dir = None
            return "hit"
        self.last_hit = None
        self.last_dir = None
        return "miss"

# ── Per-connection handler ─────────────────────────────────────────
def handle_client(conn: socket.socket, addr):
    print(f"[+] Battleship AI connection from {addr}")

    # Read HTTP request
    req = b""
    while b"\r\n\r\n" not in req:
        chunk = conn.recv(4096)
        if not chunk:
            return
        req += chunk
    headers = req.decode(errors="replace")
    key = ""
    for line in headers.split("\r\n"):
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
    if not key:
        conn.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
        conn.close()
        return

    # Send 101 handshake
    accept = ws_accept_key(key)
    resp = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
    )
    conn.sendall(resp.encode())
    print("[+] Handshake complete")

    ai = AIBattleship()
    move_num = 0

    try:
        while True:
            raw = read_ws_frame(conn)
            data = json.loads(raw)
            move_num += 1

            if data.get("type") != "shot":
                print(f"[?] Unexpected message: {data}")
                continue

            r = int(data["row"])
            c = int(data["col"])
            result = data.get("result", "miss")

            print(f"[<] Player shot {move_num}: ({r},{c}) → {result}")

            # AI takes its shot (based on the *previous* turn context)
            # The "result" here tells the AI how its *previous* shot went,
            # so it can decide hunt vs target.
            ar, ac = ai.choose_shot(result)
            ai_result = ai.resolve(ar, ac)

            out = json.dumps({"type": "shot", "row": ar, "col": ac, "result": ai_result})
            conn.sendall(build_ws_frame(out.encode()))
            print(f"[>] AI shot {move_num}: ({ar},{ac}) → {ai_result}")

            if all_ships_destroyed(ai.ships, ai.destroyed):
                game_over = {
                    "type": "game_over",
                    "result": "player_win",
                    "message": "All AI ships destroyed!",
                }
                conn.sendall(build_ws_frame(json.dumps(game_over).encode()))
                print(f"[>] Sent game_over: {game_over}")
                break

    except (ConnectionError, OSError, json.JSONDecodeError) as e:
        print(f"[-] Connection ended: {e}")
    finally:
        conn.close()
        print("[+] Connection closed")

# ── Main ───────────────────────────────────────────────────────────
def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(5)
    print(f"Battleship AI stub listening on ws://localhost:{PORT}")
    try:
        while True:
            conn, addr = srv.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            t.start()
    except KeyboardInterrupt:
        print("\nServer stopped.")
    finally:
        srv.close()

if __name__ == "__main__":
    main()
