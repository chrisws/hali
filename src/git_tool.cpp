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
  "status", "log", "diff", "add", "commit", "branch", "show",
  "rm", "config", "submodule", "stash", "tag"
};

// Reject args containing shell metacharacters that enable injection.
// The shell handles tokenization and quoting of the args string.
// We only block characters that enable command injection:
//   ;  |  &  `  $(  >  <  \n
static bool args_safe(const std::string &args) {
  if (args.empty()) return true;
  if (has_shell_metachars(args)) return false;
  if (args.find('\n') != std::string::npos) return false;
  return true;
}

// Wrap a string in single quotes for safe shell interpolation.
// Used for the sandbox path which we control.
static std::string shell_quote(const std::string &s) {
  if (s.empty()) return "''";
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

// For 'rm', require --cached to prevent destructive file removal.
static bool rm_args_valid(const std::string &args) {
  if (args.empty()) return false;
  return args.find("--cached") != std::string::npos;
}

std::string git_run(const std::string &subcommand, const std::string &args,
                    const std::string &sandbox) {
  // Validate subcommand
  if (std::find(GIT_ALLOWED.begin(), GIT_ALLOWED.end(), subcommand) == GIT_ALLOWED.end()) {
    return "ERROR: git subcommand '" + subcommand + "' is not permitted. "
           "Allowed: status, log, diff, add, commit, branch, show, "
           "rm, config, submodule, stash, tag";
  }

  // Validate args
  if (!args_safe(args)) {
    return "ERROR: args contain unsafe characters";
  }

  // For rm, require --cached
  if (subcommand == "rm" && !rm_args_valid(args)) {
    return "ERROR: git rm requires --cached flag. "
           "Bare rm is not permitted to protect history.";
  }

  // Build command: git -C <sandbox> <sub> <args>
  // Sandbox path is quoted (we control it). Args are passed through
  // for the shell to tokenize — safety is ensured by args_safe().
  std::string command = "git -C " + shell_quote(sandbox) + " " + subcommand;
  if (!args.empty()) {
    command += " " + args;
  }

  std::string out = run_shell_command(command);

  if (out.size() > 4096) {
    out = out.substr(0, 4096) + "\n…(truncated)";
  }

  return out;
}