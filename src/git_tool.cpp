// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "git_tool.h"
#include "shell_util.h"

static const std::vector<std::string> GIT_ALLOWED = {
  "status", "log", "diff", "add", "commit", "branch", "show"
};

// Reject args containing shell metacharacters or quoting chars
static bool args_safe(const std::string &args) {
  if (args.empty()) return true;
  if (has_shell_metachars(args)) return false;
  for (char c : args) {
    if (c == '$' || c == '(' || c == ')' || c == '\n' ||
        c == '"' || c == '\'') {
      return false;
    }
  }
  return true;
}

std::string git_run(const std::string &subcommand, const std::string &args,
                    const std::string &sandbox) {
  // Validate subcommand
  if (std::find(GIT_ALLOWED.begin(), GIT_ALLOWED.end(), subcommand) == GIT_ALLOWED.end()) {
    return "ERROR: git subcommand '" + subcommand + "' is not permitted. "
           "Allowed: status, log, diff, add, commit, branch, show";
  }

  // Validate args
  if (!args_safe(args)) {
    return "ERROR: args contain unsafe characters";
  }

  // Build command: git -C <sandbox> <sub> <args>
  std::string command = "git -C " + sandbox + " " + subcommand;
  if (!args.empty()) {
    command += " " + args;
  }

  std::string out = run_shell_command(command);

  if (out.size() > 4096) {
    out = out.substr(0, 4096) + "\n…(truncated)";
  }

  return out;
}