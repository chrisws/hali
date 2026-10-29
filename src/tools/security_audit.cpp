// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "security_audit.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "sha1.h"

namespace fs = std::filesystem;

namespace sec_audit {

//
// Compile-time salt: this value is embedded in the Hali binary and
// is never exposed to the model, the user, or any external process.
// The audit file hash is SHA1(sandbox_path + SALT), so only Hali
// can produce a valid marker.  A prompt-injected agent or external
// script cannot forge the file without knowing this constant.
//
static const char *SALT = "hali-sec-audit-v1-2026";

//
// Path to the audit marker file within the sandbox.
//
static std::string audit_path(const std::string &sandbox) {
  return sandbox + "/.hali/security_audit.json";
}

//
// Compute the expected hash for a given sandbox path.
//
static std::string compute_hash(const std::string &sandbox) {
  std::string input = sandbox + SALT;
  auto h = sha1::hash(input);
  std::ostringstream oss;
  for (auto b : h) {
    oss << std::hex << (b / 16) << (b % 16);
  }
  return oss.str();
}

//
// Format current time as ISO 8601 string.
//
static std::string now_iso() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&tt, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

bool is_audited(const std::string &sandbox) {
  std::string path = audit_path(sandbox);
  std::error_code ec;
  if (!fs::exists(path, ec) || ec) {
    return false;
  }

  // Read the file and extract the "hash" field.
  std::ifstream f(path);
  if (!f) {
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  f.close();

  // Simple JSON parse: look for "hash":"<value>"
  auto pos = content.find("\"hash\"");
  if (pos == std::string::npos) {
    return false;
  }
  pos = content.find(':', pos);
  if (pos == std::string::npos) {
    return false;
  }
  pos = content.find('"', pos + 1);
  if (pos == std::string::npos) {
    return false;
  }
  auto end = content.find('"', pos + 1);
  if (end == std::string::npos) {
    return false;
  }
  std::string stored_hash = content.substr(pos + 1, end - pos - 1);

  // Compare with expected hash.
  return stored_hash == compute_hash(sandbox);
}

bool mark_audited(const std::string &sandbox) {
  std::string hali_dir = sandbox + "/.hali";
  std::error_code ec;
  fs::create_directories(hali_dir, ec);
  if (ec) {
    return false;
  }

  std::string path = audit_path(sandbox);
  std::string hash = compute_hash(sandbox);
  std::string ts = now_iso();

  std::ofstream f(path);
  if (!f) {
    return false;
  }
  f << "{\n";
  f << "  \"hash\": \"" << hash << "\",\n";
  f << "  \"auditor\": \"hali\",\n";
  f << "  \"audited_at\": \"" << ts << "\",\n";
  f << "  \"version\": 1\n";
  f << "}\n";
  f.close();
  return true;
}

void clear_audited(const std::string &sandbox) {
  std::string path = audit_path(sandbox);
  std::error_code ec;
  fs::remove(path, ec);
}

std::string status_message(const std::string &sandbox) {
  if (is_audited(sandbox)) {
    return "Security audit: PASSED";
  }
  return "Security audit: NOT PERFORMED - sandbox may contain untrusted content";
}

} // namespace sec_audit