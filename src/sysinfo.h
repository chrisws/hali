// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SYSINFO_H
#define SYSINFO_H

#include <string>

// File metadata: size, type, permissions, line/word/char count (regular files only)
std::string stat_file(const std::string &path);

// System info: user, uid, gid, hostname, OS, kernel, arch, cwd
std::string sysinfo_text();

// Hex dump (xxd-style). offset_str is optional: "offset" or "offset length"
// (both decimal byte values). Output capped at 256 lines.
std::string hex_dump(const std::string &path, const std::string &offset_str);

#endif