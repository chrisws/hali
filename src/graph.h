// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <vector>
#include <optional>

//
// Data Format
//
// {
//   "type": "bar",
//   "title": "KV Cache Usage",
//   "data": [
//     {"label": "A", "value": 85},
//     {"label": "B", "value": 72},
//     {"label": "C", "value": 91}
//   ]
// }
//
// Visual Examples
//
// ┌──────────────────────────────────────────────────────────────────────────────┐
// │ Project Structure                                                            │
// └──────────────────────────────────────────────────────────────────────────────┘
//  ┌── level 1.1
//  │   ├── level 2.1 (111)
//  │   │   ├── level 3.1 (222.3)
//  │   │   └── level 3.2 (333.3)
//  │   └── level 2.2 (444.2)
//  │       └── level 3.1 (555.3)
//  ├── level 1.2 (666.1)
//  │   ├── 2.1 (777.2)
//  │   └── 2.2 (888.2)
//
// ┌──────────────────────────────────────────────────────────────────────────────┐
// │ House pets                                                                   │
// ├──────────────────────────────────────────────────────────────────────────────┤
// │ Spiders        █████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 20        │
// │ Cats           █░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 4         │
// │ Dogs           █░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 1         │
// │ A lot of ants  ██████████████████████████████████████░░░░░░░░░░░░░ 80        │
// └──────────────────────────────────────────────────────────────────────────────┘
//

struct GraphResult {
  GraphResult() : success_(true) {}
  ~GraphResult() = default;

  void set_error(const char *message) {
    message_ = std::string("ERROR: ") + message;
    success_ = false;
  }

  std::vector<std::string> data_;
  std::string message_;
  bool success_;
};

GraphResult tool_graph(int term_cols, const std::string &graph_json);
