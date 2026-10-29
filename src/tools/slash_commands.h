// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SLASH_COMMANDS_H
#define SLASH_COMMANDS_H

#include <string>
#include <vector>

struct HaliConfig;
struct Agent;
struct Tui;

struct SlashCommand {
  const char *name;
  const char *usage;
  const char *description;
};

const std::vector<SlashCommand> &slash_commands();

/**
 * @brief Dispatch a slash command (input starts with '/').
 */
void handle_slash(const std::string &input, HaliConfig &cfg, Agent &agent, Tui &tui);

#endif