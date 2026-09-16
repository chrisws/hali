// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <array>

namespace sha1 {
  std::array<uint8_t, 20> hash(const std::string &input);
}
