// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef ENGINE_FACTORY_H
#define ENGINE_FACTORY_H

#include <memory>
#include "engine.h"

std::unique_ptr<Engine> create_engine();

#endif // ENGINE_FACTORY_H