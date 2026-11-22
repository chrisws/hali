// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "engine_factory.h"
#include "llama/llama_sb.h"

std::unique_ptr<Engine> create_engine() {
  return std::make_unique<Llama>();
}
