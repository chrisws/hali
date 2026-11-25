// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "tools/mcp_test.h"

#include <string>
#include <vector>

#include "tools/curl.h"
#include "util/logging.h"

//
// Run MCP diagnostic test and return an exit code.
//
int run_mcp_test(HaliConfig &cfg, mcp::Client &mcp_client) {
  log_open_console();

  // Apply config settings (host/port) to the client
  if (!cfg.mcp_host_.empty() && cfg.mcp_port_ > 0) {
    mcp_client.set_settings(cfg.mcp_host_, cfg.mcp_port_);
  }

  const auto &s = mcp_client.get_settings();
  log_write(LEVEL_INFO, "mcp_test", "=== MCP Diagnostic ===");
  log_write(LEVEL_INFO, "mcp_test", "host:    %s", s.host_.empty() ? "(not set)" : s.host_.c_str());
  log_write(LEVEL_INFO, "mcp_test", "port:    %d", s.port_);
  log_write(LEVEL_INFO, "mcp_test", "context: %s", s.context_.c_str());
  log_write(LEVEL_INFO, "mcp_test", "filter:  %s",
            cfg.mcp_filter_.empty() ? "(none)" : cfg.mcp_filter_.front().c_str());
  if (cfg.mcp_filter_.size() > 1) {
    for (size_t i = 1; i < cfg.mcp_filter_.size(); ++i) {
      log_write(LEVEL_INFO, "mcp_test", "        %s", cfg.mcp_filter_[i].c_str());
    }
  }
  log_write(LEVEL_INFO, "mcp_test", "enabled: %s", mcp_client.enabled() ? "yes" : "no");
  log_write(LEVEL_INFO, "mcp_test", "---------------------");

  if (!mcp_client.enabled()) {
    log_write(LEVEL_ERROR, "mcp_test", "MCP is not enabled. Use --mcp to enable it.");
    curl_close();
    log_close();
    return 1;
  }

  if (s.host_.empty()) {
    log_write(LEVEL_ERROR, "mcp_test", "MCP host is not set. Provide host via mcp.json or config.");
    curl_close();
    log_close();
    return 1;
  }

  const std::string context = mcp_client.get_system_context(cfg.mcp_filter_);
  if (context.empty()) {
    log_write(LEVEL_ERROR, "mcp_test", "MCP connection failed - no tools available.");
    log_write(LEVEL_ERROR, "mcp_test", "Check that the server is running at %s:%d/%s",
              s.host_.c_str(), s.port_, s.context_.c_str());
    curl_close();
    log_close();
    return 1;
  }

  log_write(LEVEL_INFO, "mcp_test", "MCP connection successful. System context:");
  log_write(LEVEL_INFO, "mcp_test", "%s", context.c_str());
  curl_close();
  log_close();
  return 0;
}