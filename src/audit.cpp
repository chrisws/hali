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

    // Track nesting depth via braces
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (c == '{') {
        ++nesting;
        if (nesting > m.max_nesting) {
          m.max_nesting = nesting;
          m.max_nesting_line = line_num;
        }
        // Detect function start: a line that has a '{' at a depth
        // where we were at a "statement" level (depth 1 = file/class scope)
        if (nesting == 1 && func_start == -1) {
          func_start = line_num;
          func_depth_at_start = 1;
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
        if (nesting > 0) {
          --nesting;
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
    if (switch_depth > 0 && nesting <= switch_depth) {
      switch_depth = 0;
    }

    // Track parameter count in function signatures
    // Heuristic: count commas at paren depth 1 in lines that look like signatures
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
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

  return m;
}

//
// ── Convert metrics to issues ──────────────────────────────────────────────
//
static void metrics_to_issues(const FileMetrics &m, std::vector<AuditIssue> &out) {
  auto add = [&](const std::string &sev, int ln, const std::string &msg) {
    out.push_back({m.path, ln, sev, msg});
  };

  // File length
  if (m.total_lines > 2000) {
    add("high", 1, std::to_string(m.total_lines) + " lines — very large file");
  } else if (m.total_lines > 1000) {
    add("medium", 1, std::to_string(m.total_lines) + " lines — large file");
  }

  // Estimated token count (~4 chars per token)
  long est_tokens = m.file_bytes / 4;
  if (est_tokens > 15000) {
    add("high", 1, "~" + std::to_string(est_tokens) + " tokens — heavy context load");
  } else if (est_tokens > 8000) {
    add("medium", 1, "~" + std::to_string(est_tokens) + " tokens — large file");
  }

  // Longest line
  if (m.longest_line_len > 200) {
    add("high", m.longest_line_num,
        "line is " + std::to_string(m.longest_line_len) + " chars");
  } else if (m.longest_line_len > 150) {
    add("medium", m.longest_line_num,
        "line is " + std::to_string(m.longest_line_len) + " chars");
  }

  // Nesting depth
  if (m.max_nesting > 8) {
    add("high", m.max_nesting_line,
        "nesting depth " + std::to_string(m.max_nesting));
  } else if (m.max_nesting > 6) {
    add("medium", m.max_nesting_line,
        "nesting depth " + std::to_string(m.max_nesting));
  }

  // Longest function
  if (m.longest_func_lines > 200) {
    add("high", m.longest_func_start,
        "function spans " + std::to_string(m.longest_func_lines) + " lines");
  } else if (m.longest_func_lines > 100) {
    add("medium", m.longest_func_start,
        "function spans " + std::to_string(m.longest_func_lines) + " lines");
  }

  // Include count
  if (m.include_count > 15) {
    add("high", 1, std::to_string(m.include_count) + " #include directives");
  } else if (m.include_count > 10) {
    add("medium", 1, std::to_string(m.include_count) + " #include directives");
  }

  // Preprocessor conditionals
  if (m.ifdef_count > 12) {
    add("high", 1, std::to_string(m.ifdef_count) + " #if/#ifdef blocks");
  } else if (m.ifdef_count > 8) {
    add("medium", 1, std::to_string(m.ifdef_count) + " #if/#ifdef blocks");
  }

  // Macro count
  if (m.define_count > 20) {
    add("high", 1, std::to_string(m.define_count) + " #define macros");
  } else if (m.define_count > 12) {
    add("medium", 1, std::to_string(m.define_count) + " #define macros");
  }

  // Switch cases
  if (m.switch_case_max > 20) {
    add("high", m.switch_case_line,
        "switch with " + std::to_string(m.switch_case_max) + " cases");
  } else if (m.switch_case_max > 12) {
    add("medium", m.switch_case_line,
        "switch with " + std::to_string(m.switch_case_max) + " cases");
  }

  // Parameter count
  if (m.max_params > 10) {
    add("high", m.max_params_line,
        "function with " + std::to_string(m.max_params) + " parameters");
  } else if (m.max_params > 8) {
    add("medium", m.max_params_line,
        "function with " + std::to_string(m.max_params) + " parameters");
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
std::string audit_format_prompt(const AuditResult &r) {
  std::ostringstream oss;
  oss << "Codebase audit report (" << r.total_files << " files, "
      << r.total_lines << " lines, difficulty " << r.score << "/100):\n\n";
  for (const auto &i : r.issues) {
    oss << "  [" << i.severity << "] " << i.file << ":" << i.line
        << " — " << i.message << "\n";
  }
  oss << "\nPlease address the issues above. Start with the HIGH severity items.";
  return oss.str();
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