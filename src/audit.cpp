// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "audit.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "json.h"

namespace fs = std::filesystem;

//
// ── Per-file metrics ──────────────────────────────────────────────────────
//
struct FileMetrics {
  std::string path;
  int total_lines = 0;
  int longest_line_len = 0;
  int longest_line_num = 0;
  int include_count = 0;
  int ifdef_count = 0;
  int define_count = 0;
  int max_nesting = 0;
  int max_nesting_line = 0;
  int longest_func_lines = 0;
  int longest_func_start = 0;
  int switch_case_max = 0;
  int switch_case_line = 0;
  int max_params = 0;
  int max_params_line = 0;
  long file_bytes = 0;
};

//
// ── Scan a single file ─────────────────────────────────────────────────────
//
static FileMetrics scan_file(const std::string &path) {
  FileMetrics m;
  m.path = path;
  std::error_code ec;
  m.file_bytes = static_cast<long>(fs::file_size(path, ec));

  std::ifstream f(path);
  if (!f) {
    return m;
  }

  std::string line;
  int line_num = 0;
  int nesting = 0;
  int func_start = -1;
  int func_depth_at_start = 0;
  int in_func = 0; // brace depth inside current function body
  int param_commas = 0;
  int param_paren_depth = 0;
  bool in_params = false;
  int switch_depth = 0;
  int switch_cases = 0;
  int switch_start = 0;
  std::vector<int> ns_braces; // nesting levels at which namespace '{' opened
  bool pending_ns = false;

  while (std::getline(f, line)) {
    ++line_num;
    m.total_lines++;

    // Longest line
    if (static_cast<int>(line.size()) > m.longest_line_len) {
      m.longest_line_len = static_cast<int>(line.size());
      m.longest_line_num = line_num;
    }

    // Preprocessor counts
    std::string trimmed;
    {
      auto pos = line.find_first_not_of(" \t");
      if (pos != std::string::npos) {
        trimmed = line.substr(pos);
      }
    }

    if (trimmed.rfind("#include", 0) == 0) {
      m.include_count++;
    } else if (trimmed.rfind("#ifdef", 0) == 0 ||
               trimmed.rfind("#ifndef", 0) == 0 ||
               trimmed.rfind("#if ", 0) == 0) {
      m.ifdef_count++;
    } else if (trimmed.rfind("#define", 0) == 0) {
      m.define_count++;
    }

    // Detect namespace keyword so the next '{' is tracked as structural
    if (trimmed.rfind("namespace", 0) == 0 ||
        trimmed.rfind("inline namespace", 0) == 0) {
      pending_ns = true;
    }

    int ns_depth = static_cast<int>(ns_braces.size());

    // Track nesting depth via braces (skipping string/char literals and comments)
    {
      bool in_str = false;
      bool in_char = false;
      bool in_comment = false;
      for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_comment) {
          continue;
        }
        if (in_str) {
          if (c == '\\' && i + 1 < line.size()) {
            ++i;
          } else if (c == '"') {
            in_str = false;
          }
          continue;
        }
        if (in_char) {
          if (c == '\\' && i + 1 < line.size()) {
            ++i;
          } else if (c == '\'') {
            in_char = false;
          }
          continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
          in_comment = true;
          continue;
        }
        if (c == '"') {
          in_str = true;
          continue;
        }
        if (c == '\'') {
          in_char = true;
          continue;
        }
        if (c == '{') {
          ++nesting;
          if (pending_ns) {
            ns_braces.push_back(nesting);
            pending_ns = false;
          }
          int eff_depth = nesting - static_cast<int>(ns_braces.size());
          if (eff_depth > m.max_nesting) {
            m.max_nesting = eff_depth;
            m.max_nesting_line = line_num;
          }
          // Detect function start: a '{' one level above the current
          // structural scope (namespace / class)
          if (eff_depth == 1 && func_start == -1) {
            func_start = line_num;
            func_depth_at_start = nesting;
            in_func = 1;
          } else if (in_func > 0) {
            ++in_func;
          }
        } else if (c == '}') {
          if (in_func > 0) {
            --in_func;
            if (in_func == 0 && func_start > 0) {
              int func_len = line_num - func_start + 1;
              if (func_len > m.longest_func_lines) {
                m.longest_func_lines = func_len;
                m.longest_func_start = func_start;
              }
              func_start = -1;
            }
          }
          // Close a namespace if this '}' matches the innermost one
          if (!ns_braces.empty() && nesting == ns_braces.back()) {
            ns_braces.pop_back();
          }
          if (nesting > 0) {
            --nesting;
          }
        }
      }
    }

    // Track switch cases
    if (trimmed.rfind("switch", 0) == 0) {
      switch_depth = nesting;
      switch_cases = 0;
      switch_start = line_num;
    }
    if (switch_depth > 0 && trimmed.rfind("case", 0) == 0) {
      ++switch_cases;
      if (switch_cases > m.switch_case_max) {
        m.switch_case_max = switch_cases;
        m.switch_case_line = switch_start;
      }
    }
    if (switch_depth > 0 && nesting < switch_depth) {
      switch_depth = 0;
    }

    // Track parameter count in function signatures
    // Heuristic: count commas at paren depth 1 in lines that look like signatures
    {
      bool in_str = false;
      bool in_char = false;
      bool in_comment = false;
      for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_comment) {
          continue;
        }
        if (in_str) {
          if (c == '\\' && i + 1 < line.size()) {
            ++i;
          } else if (c == '"') {
            in_str = false;
          }
          continue;
        }
        if (in_char) {
          if (c == '\\' && i + 1 < line.size()) {
            ++i;
          } else if (c == '\'') {
            in_char = false;
          }
          continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
          in_comment = true;
          continue;
        }
        if (c == '"') {
          in_str = true;
          continue;
        }
        if (c == '\'') {
          in_char = true;
          continue;
        }
        if (c == '(') {
          ++param_paren_depth;
          if (param_paren_depth == 1) {
            in_params = true;
            param_commas = 0;
          }
        } else if (c == ')') {
          if (param_paren_depth == 1) {
            in_params = false;
            int params = param_commas + 1;
            if (params > m.max_params) {
              m.max_params = params;
              m.max_params_line = line_num;
            }
          }
          if (param_paren_depth > 0) {
            --param_paren_depth;
          }
        } else if (c == ',' && in_params && param_paren_depth == 1) {
          ++param_commas;
        }
      }
    }
  }

  return m;
}

//
// ── Convert metrics to issues ──────────────────────────────────────────────
//
static void metrics_to_issues(const FileMetrics &m, std::vector<AuditIssue> &out) {
  auto add = [&](const std::string &sev, int ln, const std::string &msg,
                 const std::string &why) {
    out.push_back({m.path, ln, sev, msg, why});
  };

  // File length
  if (m.total_lines > 2000) {
    add("high", 1, std::to_string(m.total_lines) + " lines — very large file",
        "Exceeds typical context windows; the agent cannot see the whole file "
        "at once, making cross-reference edits and refactors error-prone.");
  } else if (m.total_lines > 1000) {
    add("medium", 1, std::to_string(m.total_lines) + " lines — large file",
        "Large files consume significant context, leaving less room for "
        "reasoning and multi-file edits in a single turn.");
  }

  // Estimated token count (~4 chars per token)
  long est_tokens = m.file_bytes / 4;
  if (est_tokens > 15000) {
    add("high", 1, "~" + std::to_string(est_tokens) + " tokens — heavy context load",
        "Reading this file alone uses most of the available context, "
        "forcing KV flushes when combined with other files.");
  } else if (est_tokens > 8000) {
    add("medium", 1, "~" + std::to_string(est_tokens) + " tokens — large file",
        "Substantial context cost per read; limits how many files the agent "
        "can hold in working memory simultaneously.");
  }

  // Longest line
  if (m.longest_line_len > 200) {
    add("high", m.longest_line_num,
        "line is " + std::to_string(m.longest_line_len) + " chars",
        "Very long lines are harder to parse and may be truncated by tools; "
        "they also obscure logic that would be clearer on multiple lines.");
  } else if (m.longest_line_len > 150) {
    add("medium", m.longest_line_num,
        "line is " + std::to_string(m.longest_line_len) + " chars",
        "Long lines reduce readability and make targeted edits (patch/replace) "
        "more fragile.");
  }

  // Nesting depth
  if (m.max_nesting > 8) {
    add("high", m.max_nesting_line,
        "nesting depth " + std::to_string(m.max_nesting),
        "Deep nesting makes control flow hard to follow; the agent must track "
        "many open scopes simultaneously, increasing the chance of "
        "misplaced braces or logic errors.");
  } else if (m.max_nesting > 6) {
    add("medium", m.max_nesting_line,
        "nesting depth " + std::to_string(m.max_nesting),
        "Moderate nesting adds cognitive load when reasoning about edge cases "
        "and interaction between branches.");
  }

  // Longest function
  if (m.longest_func_lines > 200) {
    add("high", m.longest_func_start,
        "function spans " + std::to_string(m.longest_func_lines) + " lines",
        "Very long functions are hard to modify safely; the agent must hold "
        "the entire body in context to make a change, and small edits risk "
        "breaking unrelated logic within the same function.");
  } else if (m.longest_func_lines > 100) {
    add("medium", m.longest_func_start,
        "function spans " + std::to_string(m.longest_func_lines) + " lines",
        "Long functions are harder to test in isolation and increase the "
        "blast radius of any edit within them.");
  }

  // Include count
  if (m.include_count > 15) {
    add("high", 1, std::to_string(m.include_count) + " #include directives",
        "Many headers mean a large transitive dependency graph; the agent "
        "must track which symbols come from where, and header changes "
        "cascade widely.");
  } else if (m.include_count > 10) {
    add("medium", 1, std::to_string(m.include_count) + " #include directives",
        "A moderate number of headers increases the surface area for "
        "include-order bugs and makes the dependency graph harder to navigate.");
  }

  // Preprocessor conditionals
  if (m.ifdef_count > 12) {
    add("high", 1, std::to_string(m.ifdef_count) + " #if/#ifdef blocks",
        "Heavy conditional compilation means the agent cannot determine which "
        "code paths are active without knowing the build configuration; "
        "edits may break untested #ifdef branches.");
  } else if (m.ifdef_count > 8) {
    add("medium", 1, std::to_string(m.ifdef_count) + " #if/#ifdef blocks",
        "Conditional compilation obscures the effective code structure and "
        "makes it harder to reason about what actually compiles.");
  }

  // Macro count
  if (m.define_count > 20) {
    add("high", 1, std::to_string(m.define_count) + " #define macros",
        "Many macros obscure the real code; the agent must mentally expand "
        "each macro to understand behaviour, and macro name collisions or "
        "redefinition bugs are easy to introduce.");
  } else if (m.define_count > 12) {
    add("medium", 1, std::to_string(m.define_count) + " #define macros",
        "Macros add an indirection layer that makes code harder to search, "
        "refactor, and reason about.");
  }

  // Switch cases
  if (m.switch_case_max > 20) {
    add("high", m.switch_case_line,
        "switch with " + std::to_string(m.switch_case_max) + " cases",
        "Large switch statements are hard to modify safely; adding or "
        "reordering cases risks breaking the fall-through logic, and the "
        "agent must see the entire switch to make a correct edit.");
  } else if (m.switch_case_max > 12) {
    add("medium", m.switch_case_line,
        "switch with " + std::to_string(m.switch_case_max) + " cases",
        "Moderately large switches increase the chance of missing a case "
        "during refactoring and make the dispatch logic harder to follow.");
  }

  // Parameter count
  if (m.max_params > 10) {
    add("high", m.max_params_line,
        "function with " + std::to_string(m.max_params) + " parameters",
        "Many parameters make the call site hard to read and easy to get "
        "wrong; the agent must track positional arguments, and adding a "
        "parameter requires updating every call site.");
  } else if (m.max_params > 8) {
    add("medium", m.max_params_line,
        "function with " + std::to_string(m.max_params) + " parameters",
        "A high parameter count suggests the function is doing too much; "
        "it is harder to call correctly and harder to test in isolation.");
  }
}

//
// ── Compute a 0-100 difficulty score ───────────────────────────────────────
//
static int compute_score(const std::vector<AuditIssue> &issues) {
  int score = 0;
  for (const auto &i : issues) {
    if (i.severity == "high") {
      score += 10;
    } else if (i.severity == "medium") {
      score += 4;
    } else {
      score += 1;
    }
  }
  return std::min(score, 100);
}

//
// ── Format issues as an aligned table (vector of lines) ────────────────────
//
std::vector<std::string> audit_format_table(const AuditResult &r) {
  std::vector<std::string> lines;
  if (r.issues.empty()) {
    return lines;
  }

  // Find max file width
  size_t max_file = 0;
  for (const auto &i : r.issues) {
    if (i.file.size() > max_file) {
      max_file = i.file.size();
    }
  }
  // Cap display width to keep it readable
  if (max_file > 50) {
    max_file = 50;
  }

  auto pad = [](const std::string &s, size_t w) -> std::string {
    if (s.size() >= w) {
      return s.substr(0, w - 1) + "~";
    }
    return s + std::string(w - s.size(), ' ');
  };

  // Header
  lines.push_back(std::string(2, ' ') + pad("SEV", 6) + " " +
                  pad("FILE", max_file) + " " +
                  pad("LN", 5) + "  ISSUE");
  lines.push_back(std::string(2, ' ') + std::string(6 + 1 + max_file + 1 + 5 + 2 + 40, '-'));

  for (const auto &i : r.issues) {
    std::string sev;
    if (i.severity == "high") {
      sev = "HIGH";
    } else if (i.severity == "medium") {
      sev = "MED";
    } else {
      sev = "LOW";
    }
    lines.push_back(std::string(2, ' ') + pad(sev, 6) + " " +
                    pad(i.file, max_file) + " " +
                    pad(std::to_string(i.line), 5) + "  " + i.message);
  }

  return lines;
}

//
// ── Format the full report as a prompt for the model ───────────────────────
//
std::string audit_format_prompt(const AuditResult &r,
                                const std::string &plan_path) {
  std::ostringstream oss;
  oss << "Codebase audit report (" << r.total_files << " files, "
      << r.total_lines << " lines, difficulty " << r.score << "/100):\n\n";
  for (const auto &i : r.issues) {
    oss << "  [" << i.severity << "] " << i.file << ":" << i.line
        << " — " << i.message;
    if (!i.why.empty()) {
      oss << " (" << i.why << ")";
    }
    oss << "\n";
  }
  if (!plan_path.empty()) {
    oss << "\nThe full report is saved at: " << plan_path << "\n";
  }
  oss << "\nReview each issue above. If any are false positives (not actually a "
         "problem for working with this code), list them first, one per line, "
         "in this exact format:\n"
         "  FP <file> <line> <severity> <message>\n"
         "For example:\n"
         "  FP src/main.cpp 1 medium 12 #include directives\n"
         "If there are no false positives, write: NO_FALSE_POSITIVES\n"
         "Then address the remaining issues. Start with the HIGH severity items.\n\n"
         "IMPORTANT — KV management protocol:\n"
         "  You are inspecting multiple files. After you finish inspecting each "
         "file (reading it and noting your findings), you MUST:\n"
         "   1. Append your findings for that file to the plan file ("
         << (plan_path.empty() ? "plans/audit_report.md" : plan_path) << ").\n"
         "   2. Write the current progress to SESSION.md (which files are done, "
         "which remain, key findings so far).\n"
         "   3. Call TOOL:RESTART to flush the KV cache before moving to the "
         "next file.\n"
         "  This prevents KV overflow from accumulating file contents. "
         "Do NOT read multiple large files in a single session.";
  return oss.str();
}

//
// ── Write the report as a markdown plan file ────────────────────────────────
//
std::string audit_write_report(const AuditResult &r,
                               const std::string &path) {
  std::ostringstream oss;
  oss << "# Codebase Audit Report\n\n";
  oss << "- **Files scanned:** " << r.total_files << "\n";
  oss << "- **Total lines:** " << r.total_lines << "\n";
  oss << "- **Difficulty score:** " << r.score << "/100\n";
  oss << "- **Issues found:** " << r.issues.size() << "\n\n";

  if (r.issues.empty()) {
    oss << "No issues found.\n";
  } else {
    oss << "## Issues\n\n";
    oss << "| Severity | File | Line | Issue | Why it matters |\n";
    oss << "|----------|------|------|-------|----------------|\n";
    for (const auto &i : r.issues) {
      oss << "| " << i.severity << " | " << i.file << " | " << i.line
          << " | " << i.message << " | " << i.why << " |\n";
    }
    oss << "\n## Findings (filled in by model)\n\n";
    oss << "_The model appends its per-file findings here as it works through "
           "the list._\n";
  }

  std::error_code ec;
  auto parent = fs::path(path).parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent, ec);
    if (ec) {
      return "";
    }
  }

  std::ofstream f(path);
  if (!f) {
    return "";
  }
  f << oss.str();
  return path;
}

//
// ── Public entry point ─────────────────────────────────────────────────────
//
AuditResult audit_cpp(const std::string &root) {
  AuditResult result;
  std::vector<FileMetrics> all_metrics;

  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    return result;
  }

  // Collect .cpp, .h, .hpp, .cc, .c files
  std::vector<std::string> files;
  for (const auto &entry : fs::recursive_directory_iterator(root, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto ext = entry.path().extension().string();
    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" ||
        ext == ".cc" || ext == ".c") {
      files.push_back(entry.path().string());
    }
  }

  std::sort(files.begin(), files.end());
  result.total_files = static_cast<int>(files.size());

  for (const auto &f : files) {
    FileMetrics m = scan_file(f);
    result.total_lines += m.total_lines;
    metrics_to_issues(m, result.issues);
  }

  // Sort issues: high first, then by file
  std::sort(result.issues.begin(), result.issues.end(),
            [](const AuditIssue &a, const AuditIssue &b) {
              auto rank = [](const std::string &s) {
                if (s == "high") return 0;
                if (s == "medium") return 1;
                return 2;
              };
              int ra = rank(a.severity);
              int rb = rank(b.severity);
              if (ra != rb) return ra < rb;
              if (a.file != b.file) return a.file < b.file;
              return a.line < b.line;
            });

  result.score = compute_score(result.issues);
  return result;
}

//
// ── False-positive feedback persistence ────────────────────────────────────
//

std::vector<FalsePositive> load_false_positives(const std::string &path) {
  std::vector<FalsePositive> fps;
  std::error_code ec;
  if (!fs::exists(path, ec) || ec) {
    return fps;
  }

  std::ifstream f(path);
  if (!f) {
    return fps;
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  f.close();

  auto doc = json::JsonDoc::parse(buf.str());
  if (!doc.is_valid()) {
    return fps;
  }

  auto root = doc.get_root();
  std::vector<json::JsonValue> arr;
  if (!root.get_array("false_positives", arr)) {
    return fps;
  }

  for (const auto &item : arr) {
    FalsePositive fp;
    std::string val;
    if (item.get_str("file", val)) fp.file = val;
    int ln = 0;
    if (item.get_int("line", ln)) fp.line = ln;
    if (item.get_str("severity", val)) fp.severity = val;
    if (item.get_str("message", val)) fp.message = val;
    if (item.get_str("reason", val)) fp.reason = val;
    fps.push_back(std::move(fp));
  }

  return fps;
}

void save_false_positives(const std::string &path,
                          const std::vector<FalsePositive> &fps) {
  // Build JSON manually — the structure is simple and fixed.
  std::ostringstream oss;
  oss << "{\n  \"false_positives\": [";
  for (size_t i = 0; i < fps.size(); ++i) {
    if (i > 0) oss << ",";
    const auto &fp = fps[i];
    auto esc = [](const std::string &s) -> std::string {
      std::string out;
      for (char c : s) {
        if (c == '"' || c == '\\') {
          out += '\\';
        }
        out += c;
      }
      return out;
    };
    oss << "\n    {\n"
        << "      \"file\": \"" << esc(fp.file) << "\",\n"
        << "      \"line\": " << fp.line << ",\n"
        << "      \"severity\": \"" << esc(fp.severity) << "\",\n"
        << "      \"message\": \"" << esc(fp.message) << "\",\n"
        << "      \"reason\": \"" << esc(fp.reason) << "\"\n"
        << "    }";
  }
  if (!fps.empty()) {
    oss << "\n  ";
  }
  oss << "]\n}\n";

  std::ofstream f(path);
  if (f) {
    f << oss.str();
  }
}

void filter_false_positives(AuditResult &r,
                            const std::vector<FalsePositive> &fps) {
  if (fps.empty()) {
    return;
  }
  auto matches = [&](const AuditIssue &issue) -> bool {
    for (const auto &fp : fps) {
      if (fp.file == issue.file && fp.line == issue.line &&
          fp.severity == issue.severity && fp.message == issue.message) {
        return true;
      }
    }
    return false;
  };
  r.issues.erase(
    std::remove_if(r.issues.begin(), r.issues.end(), matches),
    r.issues.end());
  // Recompute score after filtering
  r.score = compute_score(r.issues);
}