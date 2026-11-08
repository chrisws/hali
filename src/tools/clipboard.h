// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <string>

// Internal clipboard: copy/cut/paste file lines by line number.
// a1 = command (copy|cut|paste|show|clear)
// a2 = "file start,end" for copy/cut, "file line" for paste
std::string clipboard_exec(const std::string &sandbox, const std::string &a1, const std::string &a2);

#endif