// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef AUDIT_H
#define AUDIT_H

#include <string>
#include <vector>

//
// A single issue found during codebase audit.
//
struct AuditIssue {
  std::string file;
  int line;
  std::string severity; // "high", "medium", "low"
  std::string message;
};

//
// Aggregated result of a codebase audit.
//
struct AuditResult {
  std::vector<AuditIssue> issues;
  int total_files = 0;
  int total_lines = 0;
  int score = 0; // 0-100, higher = harder for a model to work on
};

//
// Scan a directory tree for C/C++ source files and report
// issues that make the codebase difficult for an LLM to handle.
//
AuditResult audit_cpp(const std::string &root);

//
// Format issues as aligned table lines for TUI display.
//
std::vector<std::string> audit_format_table(const AuditResult &r);

//
// Format the report as a prompt string to feed back to the model.
//
std::string audit_format_prompt(const AuditResult &r);

#endif