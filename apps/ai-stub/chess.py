"""
Minimal WebSocket stub server (stdlib only, no pip packages).

Listens on ws://localhost:8000, plays 5 preset Black moves,
then sends a game_over payload.

Usage:  python3 ai-stub/server.py
"""
import socket
import hashlib
import struct
import json
import base64
import threading

PORT = 8000
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# ── Preset moves (Black) ──────────────────────────────────────────
PRESET_MOVES = [
    {"from": "e7", "to": "e5", "piece": "bP", "type": "move"},   # 1... e5
    {"from": "g8", "to": "f6", "piece": "bN", "type": "move"},   # 2... Nf6
    {"from": "b8", "to": "c6", "piece": "bN", "type": "move"},   # 3... Nc6
    {"from": "f8", "to": "e7", "piece": "bB", "type": "move"},   # 4... Be7
    {"from": "e8", "to": "g8", "piece": "bK", "type": "move"},   # 5... O-O
]
GAME_OVER = {
    "type": "game_over",
    "result": "resignation",
    "winner": "b",
    "message": "AI wins (dummy script complete).",
}

# ── WebSocket frame helpers ───────────────────────────────────────
def ws_accept_key(key: str) -> str:
    digest = hashlib.sha1((key + GUID).encode()).digest()
    return base64.b64encode(digest).decode()

def build_ws_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    """Build an unmasked server→client text frame."""
    header = bytes([0x80 | opcode, len(payload)])
    return header + payload

def read_ws_frame(sock: socket.socket) -> bytes:
    """Read one masked client→client frame, return unmasked payload."""
    hdr = sock.recv(2)
    if len(hdr) < 2:
        raise ConnectionError("short header")
    b0, b1 = hdr[0], hdr[1]
    opcode = b0 & 0x0F
    if opcode == 0x8:  # close
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

# ── Per-connection handler ────────────────────────────────────────
def handle_client(conn: socket.socket, addr):
    print(f"[+] Connection from {addr}")
    # Read HTTP request
    req = b""
    while b"\r\n\r\n" not in req:
        chunk = conn.recv(4096)
        if not chunk:
            return
        req += chunk
    headers = req.decode(errors="replace")
    # Extract Sec-WebSocket-Key
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

    # Play through preset moves
    for i, move in enumerate(PRESET_MOVES):
        try:
            data = read_ws_frame(conn)
            print(f"[<] Move {i+1} received: {data.decode(errors='replace')}")
            out = json.dumps(move)
            conn.sendall(build_ws_frame(out.encode()))
            print(f"[>] Sent Black move {i+1}: {out}")
        except (ConnectionError, OSError) as e:
            print(f"[-] Client disconnected during move {i+1}: {e}")
            return

    # Send game over
    try:
        out = json.dumps(GAME_OVER)
        conn.sendall(build_ws_frame(out.encode()))
        print(f"[>] Sent game_over: {out}")
    except (ConnectionError, OSError):
        pass
    conn.close()
    print("[+] Connection closed")

# ── Main ──────────────────────────────────────────────────────────
def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(5)
    print(f"Stub WebSocket server listening on ws://localhost:{PORT}")
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
