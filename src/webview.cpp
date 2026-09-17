// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// Serves static files from the sandbox directory and provides a
// WebSocket-based live-reload channel.  When TOOL:WRITE or TOOL:PATCH
// modifies a file, the host calls webview_broadcast_reload() to push
// a "reload" frame to every connected browser tab.
//
// Key design decisions
//   • No file watching — the trigger is TOOL:WRITE / TOOL:PATCH.
//   • HTTP server uses POSIX sockets (libcurl is a client library and
//     cannot listen).  The protocol handling follows the same RFC 7230 /
//     RFC 6455 patterns that libcurl uses internally.
//

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "webview.h"
#include "logging.h"

namespace fs = std::filesystem;

// ────────────────────────────────────────────────────────────────────────────
// Base64 encoding (for WebSocket handshake)
// ────────────────────────────────────────────────────────────────────────────

static std::string base64_encode(const uint8_t *data, size_t len) {
  static constexpr char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t octet_a = data[i];
    uint32_t octet_b = (i + 1 < len) ? data[i + 1] : 0;
    uint32_t octet_c = (i + 2 < len) ? data[i + 2] : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    out += TABLE[(triple >> 18) & 0x3F];
    out += TABLE[(triple >> 12) & 0x3F];
    out += (i + 1 < len) ? TABLE[(triple >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? TABLE[triple & 0x3F]       : '=';
  }
  return out;
}

// ────────────────────────────────────────────────────────────────────────────
// WebSocket constants
// ────────────────────────────────────────────────────────────────────────────

static constexpr std::string_view WS_PATH = "/__nitro_ws__";
static constexpr char  WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ────────────────────────────────────────────────────────────────────────────
// MIME types
// ────────────────────────────────────────────────────────────────────────────

static std::string mime_type(const std::string &path) {
  auto ext = [](const std::string &p) -> std::string {
    auto dot = p.find_last_of('.');
    if (dot == std::string::npos) return "";
    return p.substr(dot);
  };
  auto e = ext(path);
  if (e == ".html" || e == ".htm") return "text/html; charset=utf-8";
  if (e == ".js"   || e == ".mjs")  return "application/javascript";
  if (e == ".css")                   return "text/css";
  if (e == ".json")                  return "application/json";
  if (e == ".png")                   return "image/png";
  if (e == ".jpg"  || e == ".jpeg")  return "image/jpeg";
  if (e == ".gif")                   return "image/gif";
  if (e == ".svg")                   return "image/svg+xml";
  if (e == ".ico")                   return "image/x-icon";
  if (e == ".woff")                  return "font/woff";
  if (e == ".woff2")                 return "font/woff2";
  if (e == ".txt")                   return "text/plain";
  if (e == ".md")                    return "text/plain";
  if (e == ".xml")                   return "application/xml";
  if (e == ".wasm")                  return "application/wasm";
  return "application/octet-stream";
}

// ────────────────────────────────────────────────────────────────────────────
// Reload client snippet — injected into every .html response.
// ────────────────────────────────────────────────────────────────────────────

static const std::string RELOAD_SNIPPET =
  "\n<script>\n"
  "(function() {\n"
  "  var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';\n"
  "  var ws = new WebSocket(proto + location.host + '"
  + std::string(WS_PATH) +
  "');\n"
  "  ws.onmessage = function(e) {\n"
  "    if (typeof window._ws_onMessage === 'function') window._ws_onMessage(e);\n"
  "    else if (e.data === 'reload') location.reload();\n"
  "  };\n"
  "  ws.onopen = function() {\n"
  "    window._ws_sendMessage = function(msg) {\n"
  "      if (ws.readyState === WebSocket.OPEN) ws.send(msg);\n"
  "    };\n"
  "    if (typeof window._ws_onOpen === 'function') window._ws_onOpen();\n"
  "  };\n"
  "  ws.onclose   = function(e) { console.log('closed %o', e); };\n"
  "  ws.onerror   = function(e) { console.error('[%o]', e); };\n"
  "})();\n"
  "</script>\n";
// ────────────────────────────────────────────────────────────────────────────
// WebServer
// ────────────────────────────────────────────────────────────────────────────
//
// A lightweight HTTP + WebSocket server for local web development.
//
//   • Serves static files from a sandbox root directory.
//   • Injects a WebSocket live-reload client into every .html response.
//   • Accepts WebSocket upgrades on the WS_PATH endpoint.
//   • Exposes broadcast_reload() so TOOL:WRITE / TOOL:PATCH can trigger
//     a page refresh in all connected browser tabs.
//   • No file-system watching — the caller is responsible for invoking
//     broadcast_reload() at the right moment.
//
struct WebServer {
  int         port_        = 9080;
  std::string root_;
  int         listen_fd_   = -1;
  bool        running_     = false;
  bool        live_reload_ = true;

  std::vector<int> ws_clients_;
  std::mutex       ws_mutex_;
  std::atomic<bool> stop_{false};
  std::thread      accept_thread_;

  // Client → server message queue (thread-safe).
  std::vector<std::string> msg_queue_;
  std::mutex               msg_mutex_;

  // ── Lifecycle ─────────────────────────────────────────────────────
  bool start(const std::string &root, int port) {
    root_  = root;
    port_  = port;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }
    if (::listen(listen_fd_, 16) < 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }

    running_ = true;
    stop_    = false;
    accept_thread_ = std::thread([this] { accept_loop(); });
    return true;
  }

  void stop() {
    stop_    = true;
    running_ = false;
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    {
      std::lock_guard<std::mutex> lock(ws_mutex_);
      for (int fd : ws_clients_) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
      }
      ws_clients_.clear();
    }
    if (accept_thread_.joinable()) {
      accept_thread_.join();
    }
  }

  ~WebServer() { stop(); }

  // ── Public ────────────────────────────────────────────────────────
  void broadcast_message(const std::string &message) {
    if (!live_reload_) return;
    std::string frame = ws_encode_frame(message);
    std::lock_guard<std::mutex> lock(ws_mutex_);
    for (auto it = ws_clients_.begin(); it != ws_clients_.end(); ) {
      ssize_t n = ::send(*it, frame.data(), frame.size(), MSG_NOSIGNAL);
      if (n < 0) {
        ::close(*it);
        it = ws_clients_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // ── Internals ─────────────────────────────────────────────────────
  static std::string ws_encode_frame(const std::string &payload) {
    // RFC 6455: server → client text frame, unmasked, payload < 126 bytes.
    std::string frame;
    frame += static_cast<char>(0x81); // FIN=1, opcode=1 (text)
    frame += static_cast<char>(payload.size());
    frame += payload;
    return frame;
  }

  // Decode a single WebSocket frame from a byte buffer (RFC 6455).
  // Returns the number of bytes consumed, or 0 if the buffer does
  // not yet contain a complete frame.
  static size_t ws_decode_frame(const char *data, size_t len, std::string &out) {
    if (len < 2) return 0;
    uint8_t b1 = static_cast<uint8_t>(data[1]);
    bool   masked  = (b1 & 0x80) != 0;
    size_t p_len   = b1 & 0x7F;
    size_t pos     = 2;

    if (p_len == 126) {
      if (len < 4) return 0;
      p_len = (static_cast<uint16_t>(
        (static_cast<unsigned char>(data[2]) << 8) |
        static_cast<unsigned char>(data[3])));
      pos = 4;
    } else if (p_len == 127) {
      if (len < 10) return 0;
      uint64_t long_len = 0;
      for (int i = 0; i < 8; ++i) {
        long_len = (long_len << 8) | static_cast<unsigned char>(data[2 + i]);
      }
      p_len = static_cast<size_t>(long_len);
      pos = 10;
    }

    uint8_t mask[4];

    if (masked) {
      if (len < pos + 4) return 0;
      std::memcpy(mask, data + pos, 4);
      pos += 4;
    }

    if (len < pos + p_len) return 0;

    out.assign(data + pos, p_len);
    if (masked) {
      for (size_t i = 0; i < p_len; ++i) {
        out[i] = static_cast<char>(static_cast<unsigned char>(out[i]) ^ mask[i % 4]);
      }
    }

    return pos + p_len;
  }
  static std::string ws_accept_key(const std::string &client_key) {
    const std::string input = client_key + WS_GUID;
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.data()),  input.size(), digest);
    return base64_encode(digest, SHA_DIGEST_LENGTH);
  }

  void accept_loop() {
    while (!stop_) {
      sockaddr_in client_addr{};
      socklen_t   len = sizeof(client_addr);
      int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &len);
      if (client_fd < 0) {
        if (stop_) {
          break;
        }
        continue;
      }
      // Each connection is handled in a detached thread so the accept
      // loop never blocks.  For a local dev server the number of
      // concurrent connections is tiny.
      std::thread([this, client_fd] { handle_client(client_fd); }).detach();
    }
  }

  // Read the full HTTP request head (up to \r\n\r\n).
  static bool read_request_head(int fd, std::string &head) {
    head.clear();
    char buf[4096];
    while (true) {
      auto pos = head.find("\r\n\r\n");
      if (pos != std::string::npos) return true;
      ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) return false;
      head.append(buf, static_cast<size_t>(n));
      if (head.size() > 65536) return false; // safety cap
    }
  }

  // Parsed HTTP request with case-insensitive header lookup
  // that preserves the case of header *values* (important for
  // the base64 Sec-WebSocket-Key).
  struct Request {
    std::string method, path;
    std::string head;

    std::string header(const std::string &name) const {
      std::string lower_name = name;
      std::ranges::transform(lower_name, lower_name.begin(), ::tolower);

      size_t line_start = 0;
      auto nl = head.find("\r\n");
      if (nl != std::string::npos) line_start = nl + 2;

      while (line_start < head.size()) {
        auto eol = head.find("\r\n", line_start);
        if (eol == std::string::npos) eol = head.size();
        auto colon = head.find(':', line_start);
        if (colon == std::string::npos || colon > eol) break;

        // Lowercase the header name only for comparison.
        std::string hname_lower = head.substr(line_start, colon - line_start);
        std::ranges::transform(hname_lower, hname_lower.begin(), ::tolower);
        hname_lower.erase(0, hname_lower.find_first_not_of(" \t"));
        hname_lower.erase(hname_lower.find_last_not_of(" \t") + 1);

        if (hname_lower == lower_name) {
          // Return the value as-is (preserving case).
          size_t vstart = colon + 1;
          std::string val = head.substr(vstart, eol - vstart);
          val.erase(0, val.find_first_not_of(" \t"));
          val.erase(val.find_last_not_of(" \t\r\n") + 1);
          return val;
        }
        line_start = eol + 2;
      }
      return "";
    }
  };

  static Request parse_request(const std::string &head) {
    Request req;
    req.head = head;
    auto sp1 = head.find(' ');
    if (sp1 == std::string::npos) return req;
    req.method = head.substr(0, sp1);
    auto sp2 = head.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return req;
    req.path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    auto qm = req.path.find('?');
    if (qm != std::string::npos) req.path = req.path.substr(0, qm);
    return req;
  }

  static void send_http_response(int fd, int status, const std::string &status_text,
                                 const std::string &content_type,
                                 const std::string &body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " " << status_text << "\r\n";
    resp << "Content-Type: " << content_type << "\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Cache-Control: no-store\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    resp << body;
    std::string out = resp.str();
    size_t sent = 0;
    while (sent < out.size()) {
      ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
      if (n <= 0) break;
      sent += static_cast<size_t>(n);
    }
  }

  // Serve a static file, injecting the reload snippet into .html files.
  void serve_file(int fd, const std::string &rel_path) const {
    // Prevent path traversal.
    if (rel_path.find("..") != std::string::npos) {
      send_http_response(fd, 403, "Forbidden", "text/plain", "Forbidden");
      return;
    }

    fs::path file_path = fs::path(root_) / rel_path;

    // If the resolved path is a directory, look for index.html.
    if (fs::is_directory(file_path)) {
      file_path /= "index.html";
    }

    if (!fs::is_regular_file(file_path)) {
      send_http_response(fd, 404, "Not Found", "text/plain", "Not Found");
      return;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
      send_http_response(fd, 500, "Internal Server Error", "text/plain",
                         "Cannot read file");
      return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Inject the live-reload WebSocket client into HTML pages.
    if (live_reload_) {
      std::string ext = file_path.extension().string();
      if (ext == ".html" || ext == ".htm") {
        auto pos = content.find("</head>");
        if (pos != std::string::npos) {
          content.insert(pos, RELOAD_SNIPPET);
        } else {
          content += RELOAD_SNIPPET;
        }
      }
    }

    send_http_response(fd, 200, "OK", mime_type(rel_path), content);
  }

  // Per-connection handler: WebSocket upgrade or static file.
  void handle_client(int fd) {
    log_write(LEVEL_INFO, "handle request entered");

    std::string head;
    if (!read_request_head(fd, head)) {
      ::close(fd);
      return;
    }

    Request req = parse_request(head);

    // ── WebSocket upgrade ───────────────────────────────────────────
    if (req.path == std::string(WS_PATH)) {
      std::string key = req.header("sec-websocket-key");
      if (key.empty()) {
        log_write(LEVEL_INFO, "ws-upgrade bad request");
        send_http_response(fd, 400, "Bad Request", "text/plain", "Expected WebSocket upgrade request");
        ::close(fd);
        return;
      }

      std::string accept = ws_accept_key(key);
      std::ostringstream resp;
      log_write(LEVEL_INFO, "ws-upgrade [%s] key[%s] accept[%s]", req.path.c_str(), key.c_str(), accept.c_str());
      resp << "HTTP/1.1 101 Switching Protocols\r\n";
      resp << "Upgrade: websocket\r\n";
      resp << "Connection: Upgrade\r\n";
      resp << "Sec-WebSocket-Accept: " << accept << "\r\n";
      resp << "\r\n";
      std::string out = resp.str();
      size_t sent = 0;
      while (sent < out.size()) {
        ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, MSG_WAITALL);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
      }

      log_write(LEVEL_INFO, "sent [%d] of [%d] bytes", sent, out.length());
      {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        ws_clients_.push_back(fd);
      }

      // Pump the client → server channel until the browser
      // disconnects.  Each text frame is pushed onto the
      // message queue for the agent to consume.
      std::string rx;  // leftover bytes that do not yet form a
                        // complete frame
      char chunk[4096];
      while (true) {
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        rx.append(chunk, static_cast<size_t>(n));
        // Decode every complete frame in rx.
        size_t pos = 0;
        while (true) {
          std::string text;
          size_t consumed = ws_decode_frame(
            rx.data() + pos, rx.size() - pos, text);
          if (consumed == 0) break;  // incomplete — wait for more.
          {
            std::lock_guard<std::mutex> lock(msg_mutex_);
            msg_queue_.push_back(std::move(text));
          }
          pos += consumed;
        }
        rx.erase(0, pos);
      }
      log_write(LEVEL_INFO, "ws connection released");
      {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        auto it = std::ranges::find(ws_clients_, fd);
        if (it != ws_clients_.end()) ws_clients_.erase(it);
      }
      ::close(fd);
      return;
    }

    // ── Static file ─────────────────────────────────────────────────
    std::string rel_path = req.path;
    if (rel_path == "/" || rel_path.empty()) {
      rel_path = "index.html";
    } else if (!rel_path.empty() && rel_path[0] == '/') {
      rel_path = rel_path.substr(1);
    }

    log_write(LEVEL_INFO, "serve %s", rel_path.c_str());
    serve_file(fd, rel_path);
    ::close(fd);
  }
}; // struct WebServer

// ────────────────────────────────────────────────────────────────────────────
// Global instance + public API for Nitro agent integration
// ────────────────────────────────────────────────────────────────────────────
static WebServer g_webserver;

namespace webview {
  bool start(const std::string &root, int port) {
    return g_webserver.start(root, port);
  }
  void stop() {
    g_webserver.stop();
  }
  void broadcast_reload() {
    g_webserver.broadcast_message("reload");
  }
  bool is_running() {
    return g_webserver.running_;
  }
  void broadcast_message(const std::string &message) {
    g_webserver.broadcast_message(message);
  }
  bool has_message() {
    bool result;
    if (!g_webserver.running_) {
      result  = false;
    } else {
      std::lock_guard<std::mutex> lock(g_webserver.msg_mutex_);
      result = !g_webserver.msg_queue_.empty();
    }
    return result;
  }
  std::string get_message() {
    std::lock_guard<std::mutex> lock(g_webserver.msg_mutex_);
    if (g_webserver.msg_queue_.empty()) return "";
    std::string msg = std::move(g_webserver.msg_queue_.front());
    g_webserver.msg_queue_.erase(g_webserver.msg_queue_.begin());
    return msg;
  }
}
