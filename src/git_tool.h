// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef GIT_TOOL_H
#define GIT_TOOL_H

#include <string>

// Run a whitelisted git subcommand in the given sandbox directory.
// Allowed subcommands: status, log, diff, add, commit, branch, show,
//                      rm, config, submodule, stash, tag
// Returns the command output, or an ERROR string on failure.
std::string git_run(const std::string &subcommand, const std::string &args,
                    const std::string &sandbox);

#endif