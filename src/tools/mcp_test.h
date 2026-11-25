// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include "core/config.h"
#include "tools/mcp_client.h"

//
// Run MCP diagnostic test and return an exit code.
//
int run_mcp_test(HaliConfig &cfg, mcp::Client &mcp_client);