// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// basalt_printf_redirect.cpp - Wrapper implementations that route basalt
// console I/O into a pluggable sink callback.

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <dlfcn.h>

#include "basalt_printf_redirect.h"

// ---------------------------------------------------------------------------
// Sink callback (set by hali at startup; null = no-op for dev tools)
// ---------------------------------------------------------------------------

static basalt_log_sink_fn s_sink = nullptr;

void basalt_printf_redirect_set_sink(basalt_log_sink_fn sink) {
  s_sink = sink;
}

// ---------------------------------------------------------------------------
// Real function pointers (resolved once via dlsym for pass-through)
// ---------------------------------------------------------------------------

namespace {

using real_fwrite_fn  = size_t (*)(const void *, size_t, size_t, FILE *);
using real_vfprintf_fn = int (*)(FILE *, const char *, va_list);

real_fwrite_fn  s_real_fwrite   = nullptr;
real_vfprintf_fn s_real_vfprintf = nullptr;

real_fwrite_fn real_fwrite() {
  if (!s_real_fwrite)
    s_real_fwrite = (real_fwrite_fn) dlsym(RTLD_NEXT, "fwrite");
  return s_real_fwrite;
}

real_vfprintf_fn real_vfprintf() {
  if (!s_real_vfprintf)
    s_real_vfprintf = (real_vfprintf_fn) dlsym(RTLD_NEXT, "vfprintf");
  return s_real_vfprintf;
}

bool is_console(FILE *stream) {
  return stream == stderr || stream == stdout;
}

void capture(const char *data, size_t len) {
  if (s_sink && len > 0) {
    s_sink(data, len);
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Global-namespace wrappers
// ---------------------------------------------------------------------------

extern "C" {

size_t hali_basalt_fwrite(const void *ptr, size_t size, size_t count,
                          FILE *stream) {
  size_t total = size * count;
  if (is_console(stream)) {
    capture(static_cast<const char *>(ptr), total);
    return total;
  }
  auto real = real_fwrite();
  if (real) {
    return real(ptr, size, count, stream);
  }
  return 0;
}

int hali_basalt_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[4096];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    capture(buf, (size_t) n);
  }
  return n;
}

int hali_basalt_fprintf(FILE *stream, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap_copy;
  va_copy(ap_copy, ap);
  char buf[4096];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    if (is_console(stream)) {
      capture(buf, (size_t) n);
    } else {
      auto real = real_vfprintf();
      if (real) {
        n = real(stream, fmt, ap_copy);
      }
    }
  }
  va_end(ap_copy);
  return n;
}

} // extern "C"

// ---------------------------------------------------------------------------
// std-namespace wrappers (forward to global)
// ---------------------------------------------------------------------------

namespace std {

size_t hali_basalt_fwrite(const void *ptr, size_t size, size_t count,
                          FILE *stream) {
  return ::hali_basalt_fwrite(ptr, size, count, stream);
}

int hali_basalt_printf(const char *fmt, ...) {
  return ::hali_basalt_printf(fmt);
}

int hali_basalt_fprintf(FILE *stream, const char *fmt, ...) {
  return ::hali_basalt_fprintf(stream, fmt);
}

} // namespace std