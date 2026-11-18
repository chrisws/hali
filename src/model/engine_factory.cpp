// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "engine_factory.h"

#ifdef HALI_ENGINE_BASALT
#include "basalt.h"
#else
#include "llama_sb.h"
#endif

std::unique_ptr<Engine> create_engine() {
#ifdef HALI_ENGINE_BASALT
  return std::make_unique<Basalt>();
#else
  return std::make_unique<Llama>();
#endif
}