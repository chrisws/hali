// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>

// the presence of these in file.cpp breaks agent patching there
constexpr std::string PATCH_BEGIN    = "<<<<<<<";
constexpr std::string PATCH_BOUNDARY = "=======";
constexpr std::string PATCH_END      = ">>>>>>>";

std::string tool_append(const std::string &path, const std::string &data);
std::string tool_patch(const std::string& filename, const std::string& patch_str);
std::string tool_write_validate(const std::string &path, const std::string &data);
std::string tool_write(const std::string &path, const std::string &data);
std::string tool_write_backup(const std::string &backup_path, const std::string &path);
