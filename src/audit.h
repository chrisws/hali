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
  std::string why; // why this is hard for an LLM agent
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
// A false positive confirmed by the model: an audit issue that
// is not actually a problem.  Persisted to audit_feedback.json
// in the sandbox so future scans can skip it.
//
struct FalsePositive {
  std::string file;
  int line;
  std::string severity;
  std::string message;
  std::string reason;
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
// The prompt asks the model to flag false positives (one per line,
// prefixed "FP ") before fixing the remaining issues.
// If plan_path is non-empty, the prompt references it and includes
// KV-management restart instructions.
//
std::string audit_format_prompt(const AuditResult &r,
                                const std::string &plan_path = "");

//
// Load confirmed false positives from a JSON file.
// Returns an empty vector if the file does not exist.
//
std::vector<FalsePositive> load_false_positives(const std::string &path);

//
// Save confirmed false positives to a JSON file (overwrites).
//
void save_false_positives(const std::string &path,
                          const std::vector<FalsePositive> &fps);

//
// Remove issues from the result that match a known false positive.
// Matching is on file + line + severity + message.
//
void filter_false_positives(AuditResult &r,
                            const std::vector<FalsePositive> &fps);

//
// Write the audit report as a markdown plan file.
// Returns the path written, or empty string on failure.
//
std::string audit_write_report(const AuditResult &r,
                               const std::string &path);

#endif