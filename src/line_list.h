// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

//
// Line-list file I/O — one string per line.
//
// load_lines: appends non-empty lines from `path` into `out`.
//             Silently succeeds if the file does not exist.
// save_lines: writes each string in `lines` on its own line to `path`.
//             Creates parent directories as needed.
//

inline void load_lines(const std::string &path, std::vector<std::string> &out) {
  std::ifstream f(path);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty()) out.push_back(line);
  }
}

inline void save_lines(const std::string &path, const std::vector<std::string> &lines) {
  fs::path dir = fs::path(path).parent_path();
  std::error_code ec;
  fs::create_directories(dir, ec);

  std::ofstream f(path, std::ios::trunc);
  if (!f) return;
  for (const auto &s : lines) {
    f << s << '\n';
  }
}