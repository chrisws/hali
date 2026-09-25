// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <sstream>
#include <regex>
#include <fstream>
#include <filesystem>
#include <format>
#include <atomic>

#include "mcp_client.h"
#include "mcp_format.h"
#include "curl.h"
#include "json.h"
#include "logging.h"
#include "string_utils.h"

// https://modelcontextprotocol.io/specification/2025-03-26/basic/lifecycle

using namespace mcp;

//
// Reads Mcp-Session-Id from the headers
//
static size_t header_callback(const char *buffer, size_t size, const size_t nItems, void *userdata) {
  const size_t total = size * nItems;
  auto *session_id_out = static_cast<std::string*>(userdata);
  const std::string line(buffer, total);
  const std::string prefix = "Mcp-Session-Id:";

  log_write(LEVEL_INFO, "HTTP headers [%s]", utils::trim(line).c_str());

  // case-sensitive check is risky — HTTP headers are case-insensitive.
  // Do a case-insensitive compare instead:
  if (line.size() >= prefix.size() &&
      strncasecmp(line.c_str(), prefix.c_str(), prefix.size()) == 0) {
    const std::string value = line.substr(prefix.size());
    // trim leading/trailing whitespace and \r\n
    const size_t start = value.find_first_not_of(" \t");
    const size_t end = value.find_last_not_of(" \t\r\n");
    if (start != std::string::npos) {
      *session_id_out = value.substr(start, end - start + 1);
    }
  }

  // must return the number of bytes "handled" - libcurl aborts if you return anything else
  return total;
}

//
// SSE stream: we don't care about server-pushed content, just discard it.
//
static size_t sse_write_callback(char *ptr, size_t size, size_t nmemb, void*) {
  // const size_t total = size * nmemb;
  // log_write(DEBUG_LEVEL, "SSE: [%.*s]", (int)total, ptr);
  return size * nmemb;
}

//
// Progress callback used purely so we can cancel the blocking SSE perform()
// from another thread. libcurl invokes this periodically (at least ~1/sec)
// even while idle waiting for data, so returning non-zero here breaks the
// transfer cleanly instead of us having to kill the socket directly.
//
static int sse_progress_callback(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto *stop = static_cast<std::atomic<bool>*>(clientp);
  return stop->load() ? 1 : 0; // non-zero aborts the transfer
}

//
// Try to load MCP settings from mcp.json or mcp-settings.json in the sandbox
// Returns empty string if file not found (MCP functionality not available)
//
static Settings load_settings() {
  Settings result;
  std::string settings_path;

  result.context_ = "stream";

  // Try mcp.json first
  if (std::filesystem::exists("mcp.json")) {
    settings_path = "mcp.json";
  }

  // Try mcp-settings.json
  else if (std::filesystem::exists("mcp-settings.json")) {
    settings_path = "mcp-settings.json";
  }

  if (settings_path.empty()) {
    return result;
  }

  std::ifstream file(settings_path);
  if (!file.is_open()) {
    return result;
  }

  std::ostringstream oss;
  oss << file.rdbuf();
  std::string content = oss.str();

  try {
    auto doc = json::parse(content);
    if (!doc.is_valid()) {
      return result;
    }

    auto root = doc.get_root();
    if (!root.is_valid()) {
      return result;
    }

    if (auto server = root.get_child("server"); server.is_object()) {
      if (server.get_str("host", result.host_) &&
          server.get_int("port", result.port_)) {
        server.get_str("context", result.context_);
        return result;
      }
    }

    // alternative: top-level host/port
    if (root.get_str("host", result.host_) &&
        root.get_int("port", result.port_)) {
      root.get_str("context", result.context_);
      return result;
    }
  } catch (...) {
    log_write(LEVEL_ERROR, "JSON parsing failed");
  }

  return result;
}

Client::Client()
  : settings_(load_settings())
  , enabled_(false)
  , curl_(curl_easy_init())
  , sse_curl_(nullptr)
  , sse_stop_(false) {
}

Client::~Client() {
  disconnect();
}

void Client::disconnect() {
  log_write(LEVEL_DEBUG, "disconnect entered");
  sse_stop_.store(true);

  if (sse_thread_.joinable()) {
    sse_thread_.join();
    log_write(LEVEL_DEBUG, "thread join completed");
  }
  if (curl_) {
    log_write(LEVEL_DEBUG, "cleanup curl");
    curl_easy_cleanup(curl_);
    curl_ = nullptr;
  }
  session_id_.clear();
}

bool Client::notify_initialized() const {
  const auto doc = json::parse_mutable("");
  auto root = doc.get_root();
  root.set_str("jsonrpc", "2.0");
  root.set_str("method", "notifications/initialized");
  const auto response = send_request(doc.to_string());

  log_write(LogLevel::LEVEL_INFO, "notifications/initialized response: [%s]", response.c_str());
  return true;
}

//
// Holds the Streamable HTTP "announcement channel" open for the lifetime of
// the session. CLion's MCP server (like other 2025-11-25-era servers) tears
// the session down ~15s after initialize if it never sees this GET land, so
// this must be started right after a session id is obtained.
//
void Client::start_sse_stream() {
  sse_stop_.store(false);
  sse_curl_ = curl_easy_init();
  if (!sse_curl_) {
    log_write(LEVEL_ERROR, "failed to init curl for SSE stream");
    return;
  }

  const std::string url = std::format("http://{}:{}/{}", settings_.host_, settings_.port_, settings_.context_);
  const std::string session_header = "Mcp-Session-Id: " + session_id_;

  sse_thread_ = std::thread([this, url, session_header]() {
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    headers = curl_slist_append(headers, session_header.c_str());

    curl_easy_setopt(sse_curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(sse_curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(sse_curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(sse_curl_, CURLOPT_WRITEFUNCTION, sse_write_callback);
    curl_easy_setopt(sse_curl_, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(sse_curl_, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(sse_curl_, CURLOPT_XFERINFOFUNCTION, sse_progress_callback);
    curl_easy_setopt(sse_curl_, CURLOPT_XFERINFODATA, &sse_stop_);

    log_write(LEVEL_INFO, "SSE stream: opening for sessionId [%s]", session_id_.c_str());
    const CURLcode res = curl_easy_perform(sse_curl_);
    if (res != CURLE_OK && res != CURLE_ABORTED_BY_CALLBACK) {
      log_write(LEVEL_ERROR, "SSE stream: ended with error [%s]", curl_easy_strerror(res));
    } else {
      log_write(LEVEL_INFO, "SSE stream: closed for sessionId [%s]", session_id_.c_str());
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(sse_curl_);
    sse_curl_ = nullptr;
    log_write(LEVEL_INFO, "leaving SSE stream thread");
  });
}

std::string Client::get_system_context(const std::vector<std::string> &filter) {
  std::string p;
  if (connect()) {
    log_write(LEVEL_INFO, "Appending MCP tools");
    p += "## MCP tool\n";
    p += "TOOL:MCP <tool-name> <json-request> Invoke the named MCP tool along with with JSON request\n";
    p += "## Rules\n";
    p += "- any field named `projectPath` should be populated with the sandbox name\n";
    p += "## Available tools\n";
    for (const std::vector<Tool> tools = list_tools(); const auto &tool : tools) {
      if (filter.empty()) {
        p += tool.spec_;
      } else  {
        for (const auto &next_filter : filter) {
          if (utils::starts_with(tool.name_, next_filter)) {
            p += tool.spec_;
            break;
          }
        }
      }
    }
  } else {
    log_write(LEVEL_INFO, "Failed to connect");
  }
  return p;
}

bool Client::connect() {
  if (settings_.host_.empty()) {
    return false;
  }

  // Initialize curl
  if (!curl_) {
    log_write(LEVEL_ERROR, "failed to init curl");
    return false;
  }

  curl_set_opts(curl_);

  // Initialize handshake using mutable API
  const auto doc = json::parse_mutable("");
  if (!doc.is_valid()) {
    log_write(LEVEL_ERROR, "failed to build json doc");
    return false;
  }

  auto root = doc.get_root();
  if (!root.is_valid()) {
    log_write(LEVEL_ERROR, "failed to build json root");
    return false;
  }

  // '{"jsonrpc":"2.0","id":1,"method":"initialize",
  //      "params":{"protocolVersion":"2025-06-18","capabilities":{},
  //                "clientInfo":{"name":"hali","version":"0.1"}}}'

  // Add jsonrpc and id fields
  root.set_str("jsonrpc", "2.0");
  root.set_int("id", 1);
  root.set_str("method", "initialize");

  auto params = doc.get_child("params");
  params.set_str("protocolVersion", "2025-06-18");
  params.set_empty_obj("capabilities");

  auto clientInfo = params.get_child("clientInfo");
  clientInfo.set_str("name", "hali");
  clientInfo.set_str("version", "1.0.0");

  // Convert to string
  const std::string params_str = doc.to_string();
  if (params_str.empty()) {
    log_write(LEVEL_ERROR, "failed to build json string");
    return false;
  }

  // Send request and get session ID
  const auto response = send_request(params_str);

  if (response.empty()) {
    log_write(LogLevel::LEVEL_ERROR, "mcp request failed");
    return false;
  }

  if (auto resp_doc = json::parse(response); resp_doc.is_valid()) {
    const auto resp_root = resp_doc.get_root();
    if (int id; !resp_root.get_int("id", id)) {
      log_write(LEVEL_INFO, "failed to read id field");
    } else {
      log_write(LEVEL_INFO, "id=[%d]", id);
      if (!notify_initialized()) {
        return false;
      }
      if (!session_id_.empty()) {
        start_sse_stream();
      }
      return true;
    }
  } else {
    log_write(LEVEL_ERROR, "failed to parse response");
  }

  return false;
}

std::vector<Tool> Client::list_tools() const {
  std::vector<Tool> tools;

  if (!curl_) {
    log_write(LEVEL_ERROR, "list_tools failed - curl not initialised");
    return tools;
  }

  // Request tools/list using mutable API
  const auto doc = json::parse_mutable("");
  if (!doc.is_valid()) {
    log_write(LEVEL_ERROR, "failed to build json doc");
    return tools;
  }

  auto root = doc.get_root();
  if (!root.is_valid()) {
    log_write(LEVEL_ERROR, "failed to build json root");
    return tools;
  }

  // Add jsonrpc and id fields
  root.set_str("jsonrpc", "2.0");
  root.set_int("id", 2);

  // Set method
  root.set_str("method", "tools/list");

  // Set empty params
  auto params = doc.get_child("params");
  params.set_empty_obj("arguments");

  const std::string params_str = doc.to_string();
  if (params_str.empty()) {
    log_write(LEVEL_ERROR, "failed to build json string");
    return tools;
  }

  // Send request
  const std::string response = send_request(params_str);

  if (response.empty()) {
    log_write(LEVEL_ERROR, "list tools failed");
    return tools;
  }

  // Parse response using immutable API
  const auto resp_doc = json::parse(response);
  if (!resp_doc.is_valid()) {
    log_write(LEVEL_ERROR, "failed to parse [%s]", response.c_str());
    return tools;
  }

  const auto resp_root = resp_doc.get_root();

  if (!resp_root.is_object() || resp_root.has_string_key("result")) {
    log_write(LEVEL_ERROR, "result is not an object");
    return tools;
  }

  std::vector<json::JsonValue> vec;
  const auto result_node = resp_root.get_child("result");
  if (!result_node.is_object()) {
    log_write(LEVEL_ERROR, "result is not an object");
    return tools;
  }

  if (!result_node.get_array("tools", vec)) {
    log_write(LEVEL_ERROR, "tools is not an object os result");
    return tools;
  }

  for (const auto &tool: vec) {
    if (!tool.is_object()) {
      log_write(LEVEL_ERROR, "tools is not an object os result");
    } else {
      Tool mcp_tool;
      tool.get_str("name", mcp_tool.name_);
      tool.get_str("description", mcp_tool.description_);
      mcp_tool.spec_ = formatSpec(tool);
      tools.push_back(mcp_tool);
      log_write(LEVEL_INFO, "Found tool: [%s]", mcp_tool.name_.c_str());
    }
  }

  log_write(LEVEL_INFO, "list tools success - found [%d] tools", tools.size());
  return tools;
}

std::string Client::send_request(const std::string &request_body) const {
  std::string body;
  body.reserve(4096);

  curl_slist *curl_headers = nullptr;
  curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");
  curl_headers = curl_slist_append(curl_headers, "Accept: application/json, text/event-stream");
  if (!session_id_.empty()) {
    const std::string session_header = "Mcp-Session-Id: " + session_id_;
    curl_headers = curl_slist_append(curl_headers, session_header.c_str());
  }

  const std::string base_url = std::format("http://{}:{}/{}", settings_.host_, settings_.port_, settings_.context_);
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, curl_headers);
  curl_easy_setopt(curl_, CURLOPT_URL, base_url.c_str());
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, request_body.c_str());
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, request_body.length());
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &session_id_);

  log_write(LEVEL_INFO, "POST: sessionId:[%s] body:[%s]", session_id_.c_str(), request_body.c_str());

  const CURLcode res = curl_easy_perform(curl_);
  long http_code = 0;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

  if (curl_headers) {
    curl_slist_free_all(curl_headers);
  }

  if (res != CURLE_OK) {
    log_write(LEVEL_ERROR, "ERROR: curl: %s [%s]", curl_easy_strerror(res), base_url.c_str());
    return std::format("ERROR: curl: {}", curl_easy_strerror(res));
  }
  if (http_code >= 400) {
    log_write(LEVEL_ERROR, "ERROR: HTTP %s %s", std::to_string(http_code).c_str(), body.c_str());
    return std::format("ERROR: HTTP {} {}", std::to_string(http_code), body);
  }

  log_write(LEVEL_DEBUG, "received [%s]", body.c_str());
  log_write(LEVEL_DEBUG, "sessionId [%s]", session_id_.c_str());

  return body;
}

std::string Client::call_tool(const std::string &name, const std::string &args_str) const {
  if (!curl_) {
    return "Not connected to MCP server";
  }

  auto doc = json::parse_mutable("");
  if (!doc.is_valid()) {
    return "Failed to create request";
  }

  auto root = doc.get_root();
  if (!root.is_valid()) {
    return "Failed to create request";
  }

  root.set_str("jsonrpc", "2.0");
  root.set_int("id", 3);
  root.set_str("method", "tools/call");

  auto params = doc.get_child("params");
  params.set_str("name", name);
  params.set_obj("arguments", args_str);

  std::string request_str = doc.to_string();
  if (request_str.empty()) {
    return "Failed to serialize request";
  }

  std::string response = send_request(request_str);
  if (response.empty()) {
    return "Failed to communicate with MCP server";
  }

  log_write(LEVEL_DEBUG, "result: [%s]", response.c_str());
  return response;
}

