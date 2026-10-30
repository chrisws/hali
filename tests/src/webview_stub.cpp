// Test stub for webview — provides a functional in-memory
// message queue so tests can verify broadcast/has/get flow.
#include "webview.h"

#include <mutex>
#include <string>
#include <vector>

namespace {
  std::vector<std::string> g_queue;
  std::mutex                g_mutex;
  bool                      g_running = false;
}

namespace webview {
  bool start(const std::string &root, int port) {
    (void)root;
    (void)port;
    g_running = true;
    return true;
  }
  void stop() {
    g_running = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.clear();
  }
  void broadcast_reload() {
    // no-op in tests
  }
  void broadcast_message(const std::string &message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.push_back(message);
  }
  bool is_running() {
    return g_running;
  }
  bool has_message() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return !g_queue.empty();
  }
  std::string get_message() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.empty()) return "";
    std::string msg = std::move(g_queue.front());
    g_queue.erase(g_queue.begin());
    return msg;
  }
}