// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef PLATFORM_H
#define PLATFORM_H

//
// Platform detection via compiler preprocessor defines.
// Header-only, no dependencies.
//
enum class Platform {
  Linux,
  macOS,
  Windows,
  Unknown
};

inline Platform current_platform() {
#if defined(__linux__)
  return Platform::Linux;
#elif defined(__APPLE__)
  return Platform::macOS;
#elif defined(_WIN32)
  return Platform::Windows;
#else
  return Platform::Unknown;
#endif
}

inline const char *platform_name(Platform p) {
  switch (p) {
    case Platform::Linux:   return "Linux";
    case Platform::macOS:   return "macOS";
    case Platform::Windows: return "Windows";
    default:                return "unknown";
  }
}

#endif