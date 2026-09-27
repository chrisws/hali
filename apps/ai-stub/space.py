"""
Minimal WebSocket stub server for Space Adventure Game (stdlib only, no pip packages).

Listens on ws://localhost:8002, echoes commands back with simple responses.
This is a test harness — the real AI will be HALI.

Protocol (JSON, text frames):
  Client sends: {"type":"command","text":"go north"}
  Server sends: {"type":"response","text":"You go north."}
  Server sends: {"type":"response","text":"You see a robot here."}

Usage:  python3 ai-stub/space.py
"""
import socket
import hashlib
import json
import base64
import threading

PORT = 8002
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# ── Simple game state ──────────────────────────────────────────────
class SpaceGameState:
    def __init__(self):
        self.current_room = "bridge"
        self.inventory = []
        self.rooms = {
            "bridge": {
                "name": "Bridge",
                "description": "You're on the bridge of the U.S.S. Panucci's Pizza Delivery.",
                "exits": ["south"],
                "items": ["pizza"],
                "npcs": []
            },
            "cargo_bay": {
                "name": "Cargo Bay",
                "description": "A vast cargo bay filled with crates. There's a robot here.",
                "exits": ["north", "east"],
                "items": ["wrench", "flashlight"],
                "npcs": ["robot"]
            },
            "maintenance": {
                "name": "Maintenance Shaft",
                "description": "A narrow maintenance shaft with exposed wiring.",
                "exits": ["south", "down"],
                "items": ["keycard"],
                "npcs": []
            },
            "engine_room": {
                "name": "Engine Room",
                "description": "The heart of the ship. Massive engines hum with power.",
                "exits": ["up"],
                "items": ["warp_core_sample"],
                "npcs": []
            },
            "airlock": {
                "name": "Airlock",
                "description": "A small airlock chamber. The stars stretch out before you.",
                "exits": ["west"],
                "items": [],
                "npcs": []
            },
            "escape_pod": {
                "name": "Escape Pod Bay",
                "description": "A small room containing a single escape pod.",
                "exits": ["north"],
                "items": ["spacesuit"],
                "npcs": []
            }
        }
        self.npcs = {
            "robot": {
                "name": "Robot",
                "location": "cargo_bay",
                "dialogue": {
                    "default": "BEEP BOOP. I am R-2P2, the ship's maintenance robot.",
                    "help": "I can help you escape, but I need power.",
                    "thanks": "BEEP BOOP! Thank you! The escape pod bay is now accessible."
                }
            }
        }
        self.items = {
            "pizza": {"name": "Pepperoni Pizza", "description": "A delicious pepperoni pizza."},
            "wrench": {"name": "Wrench", "description": "A sturdy wrench."},
            "flashlight": {"name": "Flashlight", "description": "A standard-issue flashlight."},
            "keycard": {"name": "Keycard", "description": "A blue keycard with 'LEVEL 5 CLEARANCE'."},
            "warp_core_sample": {"name": "Warp Core Sample", "description": "A glowing warp core sample."},
            "spacesuit": {"name": "Spacesuit", "description": "A full spacesuit."}
        }

    def process_command(self, text):
        """Process a command and return a list of response strings."""
        text = text.lower().strip()
        parts = text.split()
        if not parts:
            return ["I don't understand that command."]

        verb = parts[0]
        noun = " ".join(parts[1:])

        if verb == "look":
            room = self.rooms.get(self.current_room)
            if room:
                responses = [room["description"]]
                if room["exits"]:
                    responses.append(f"Exits: {', '.join(room['exits'])}")
                if room["items"]:
                    item_names = [self.items[i]["name"] for i in room["items"]]
                    responses.append(f"You can see: {', '.join(item_names)}")
                if room["npcs"]:
                    npc_names = [self.npcs[n]["name"] for n in room["npcs"]]
                    responses.append(f"You see: {', '.join(npc_names)}")
                return responses
            return ["You're in an unknown location."]

        elif verb == "go" or verb in ["north", "south", "east", "west", "up", "down"]:
            direction = verb if verb in ["north", "south", "east", "west", "up", "down"] else noun
            room = self.rooms.get(self.current_room)
            if room and direction in room["exits"]:
                self.current_room = direction
                return [f"You go {direction}.", self.rooms[self.current_room]["description"]]
            else:
                return [f"You can't go {direction} from here."]

        elif verb == "take" or verb == "get":
            if not noun:
                return ["Take what?"]
            room = self.rooms.get(self.current_room)
            if room and noun in room["items"]:
                room["items"].remove(noun)
                self.inventory.append(noun)
                return [f"You take the {self.items[noun]['name']}."]
            else:
                return [f"You don't see '{noun}' here."]

        elif verb == "use":
            if not noun:
                return ["Use what?"]
            if noun in self.inventory:
                return [f"You use the {self.items[noun]['name']}. Nothing happens... yet."]
            else:
                return [f"You don't have '{noun}'."]

        elif verb == "talk" and noun.startswith("to "):
            target = noun[3:]
            room = self.rooms.get(self.current_room)
            if room and target in room["npcs"]:
                npc = self.npcs[target]
                return [f"You talk to the {npc['name']}.", npc["dialogue"]["default"]]
            else:
                return [f"You don't see '{target}' here."]

        elif verb == "inventory" or verb == "i":
            if self.inventory:
                item_names = [self.items[i]["name"] for i in self.inventory]
                return [f"You are carrying: {', '.join(item_names)}"]
            else:
                return ["You are carrying nothing."]

        elif verb == "help":
            return [
                "Available commands:",
                "  look - Look around",
                "  go [direction] - Move north, south, east, west, up, down",
                "  take [item] - Pick up an item",
                "  use [item] - Use an item",
                "  talk to [npc] - Talk to someone",
                "  inventory - Check your inventory",
                "  help - Show this help"
            ]

        else:
            return [f"I don't understand '{verb}'. Type 'help' for commands."]

# ── WebSocket frame helpers ────────────────────────────────────────
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

# ── Per-connection handler ─────────────────────────────────────────
def handle_client(conn: socket.socket, addr):
    print(f"[+] Space Adventure connection from {addr}")

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

    game = SpaceGameState()

    try:
        while True:
            raw = read_ws_frame(conn)
            data = json.loads(raw)

            if data.get("type") != "command":
                print(f"[?] Unexpected message: {data}")
                continue

            text = data.get("text", "")
            print(f"[<] Player: {text}")

            # Process command
            responses = game.process_command(text)

            # Send responses
            for response in responses:
                out = json.dumps({"type": "response", "text": response})
                conn.sendall(build_ws_frame(out.encode()))
                print(f"[>] AI: {response}")

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
    print(f"Space Adventure stub listening on ws://localhost:{PORT}")
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