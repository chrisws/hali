// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef MCP_FORMAT_H
#define MCP_FORMAT_H

#include <string>
#include "json.h"

std::string formatSpec(const json::JsonValue &root);

#endif