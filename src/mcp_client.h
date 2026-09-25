// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <curl/curl.h>

namespace mcp {

struct Tool {
  std::string name_;
  std::string description_;
  std::string spec_;
};

struct Settings {
  std::string context_;
  std::string host_;
  int port_;
};

class Client {
  public:
  Client();
  virtual ~Client();

  bool enabled() const { return enabled_; }
  void enable() { enabled_ = true; }

  bool connect();
  void disconnect();
  std::vector<Tool> list_tools() const;
  std::string get_system_context(const std::vector<std::string> &filter);
  std::string call_tool(const std::string &name, const std::string &args) const;

  private:
  Settings settings_;
  std::string session_id_;
  bool enabled_;
  CURL *curl_;
  CURL *sse_curl_;
  std::thread sse_thread_;
  std::atomic<bool> sse_stop_;

  bool notify_initialized() const;
  void start_sse_stream();
  std::string send_request(const std::string &params) const;
};

}
