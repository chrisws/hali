// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cstdio>
#include <string>

#include "shell_util.h"

std::string run_shell_command(const std::string &cmd) {
  const std::string full_cmd = cmd + " 2>&1";
  FILE *fp = popen(full_cmd.c_str(), "r");
  if (!fp) {
    return "ERROR: popen failed";
  }
  std::string out;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp)) {
    out += buf;
  }
  int rc = pclose(fp);
  if (rc != 0) {
    out += "\n[exit code: " + std::to_string(rc) + "]";
  }
  return out;
}

bool has_shell_metachars(const std::string &pattern) {
  return pattern.find(';') != std::string::npos ||
         pattern.find('|') != std::string::npos ||
         pattern.find('&') != std::string::npos ||
         pattern.find('`') != std::string::npos ||
         pattern.find("$(") != std::string::npos ||
         pattern.find('>') != std::string::npos ||
         pattern.find('<') != std::string::npos;
}