// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "slash_commands.h"

const std::vector<SlashCommand> &slash_commands() {
  static const std::vector<SlashCommand> commands = {
    {"/model",    "[path]",            "load a GGUF model (picker if no path)"},
    {"/embed",    "[path]",            "load an embedding model (picker if no path)"},
    {"/rag",      "[path]",            "index file or directory (picker if no path)"},
    {"/memory",   "",                  "KV / VRAM / layer stats"},
    {"/clear",    "",                  "reset conversation"},
    {"/settings", "",                  "show current settings"},
    {"/theme",    "",                  "toggle the theme (F12)"},
    {"/audio",    "[test|volume <v>]", "enable/disable audio, test tone, set volume"},
    {"/set",      "<key> <value>",     "change a setting live"},
    {"/save",     "[file]",            "save chat transcript (plain text)"},
    {"/session",  "[file]",            "save structured session snapshot"},
    {"/allow",        "[path]",        "allow read access to a directory"},
    {"/allow-list",   "",              "list allowed directories"},
    {"/allow-remove", "[path]",        "remove an allowed directory"},
    {"/revoke",       "<name>",        "remove an approved RUN command"},
    {"/audit",    "[path]",            "scan codebase for model-hostility issues"},
    {"/help",     "",                  "this message"},
  };
  return commands;
}