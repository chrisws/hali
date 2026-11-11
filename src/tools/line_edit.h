// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef LINE_EDIT_H
#define LINE_EDIT_H

#include <string>

// Line editing tool: read, write, insert, delete, copy, cut, paste file lines.
//
// a1 = command (read|write|insert|delete|copy|cut|paste|show|clear)
// a2 = arguments:
//   read:   "file" or "file start,end"
//   write:  "file line content"
//   insert: "file line content"
//   delete: "file start,end"
//   copy:   "file start,end"
//   cut:    "file start,end"
//   paste:  "file line"
//   show:   ""
//   clear:  ""
//
// Returns a string result with OK/ERROR prefix.
// Edit operations include surrounding context with line numbers.
std::string line_edit_exec(const std::string &sandbox, const std::string &a1, const std::string &a2);

#endif