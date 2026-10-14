// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SHA1_H
#define SHA1_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <array>

namespace sha1 {
  std::array<uint8_t, 20> hash(const std::string &input);
}

#endif