// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// console_capture.cpp - printf interposition for the basalt engine.

#include "console_capture.h"

#include <cstdarg>
#include <cstdio>
#include <dlfcn.h>
#include <mutex>

namespace {

std::function<void(const std::string &)> g_sink;
std::mutex g_mutex;

using vprintf_fn = int (*)(const char *, va_list);
using vfprintf_fn = int (*)(FILE *, const char *, va_list);

vprintf_fn real_vprintf = nullptr;
vfprintf_fn real_vfprintf = nullptr;

void ensure_real() {
  if (!real_vprintf) {
    real_vprintf = (vprintf_fn) dlsym(RTLD_NEXT, "vprintf");
    real_vfprintf = (vfprintf_fn) dlsym(RTLD_NEXT, "vfprintf");
  }
}

}  // namespace

namespace console_capture {

void set_sink(std::function<void(const std::string &)> sink) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sink = std::move(sink);
}

bool active() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_sink != nullptr;
}

}  // namespace console_capture

extern "C" {

int vprintf(const char *fmt, va_list ap) {
  ensure_real();
  char buf[8192];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n < 0) {
    return n;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_sink) {
    g_sink(std::string(buf, (size_t)n));
  } else if (real_vprintf) {
    return real_vprintf(fmt, ap);
  }
  return n;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vprintf(fmt, ap);
  va_end(ap);
  return n;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
  ensure_real();
  char buf[8192];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n < 0) {
    return n;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_sink) {
    g_sink(std::string(buf, (size_t)n));
  } else if (real_vfprintf) {
    return real_vfprintf(stream, fmt, ap);
  }
  return n;
}

int fprintf(FILE *stream, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfprintf(stream, fmt, ap);
  va_end(ap);
  return n;
}

}  // extern "C"
