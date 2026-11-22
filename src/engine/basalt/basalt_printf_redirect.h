// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// basalt_printf_redirect.h - Preprocessor redirect for basalt console I/O.
//
// Force-included via -include into every basalt translation unit.
// Renames fwrite / printf / fprintf to hali_basalt_* wrappers so that
// console output from the basalt engine is routed to a pluggable sink
// instead of corrupting the notcurses terminal.
//
// snprintf / vsnprintf are intentionally NOT redirected - they are used
// for internal buffer formatting, not console output.

#ifndef BASALT_PRINTF_REDIRECT_H
#define BASALT_PRINTF_REDIRECT_H

#ifdef __cplusplus
#include <cstdarg>
#include <cstdio>
#include <cstddef>
#else
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#endif

// --- sink callback type ----------------------------------------------------
// hali sets this at startup; basalt dev-tools leave it null (no-op).
typedef void (*basalt_log_sink_fn)(const char *data, size_t len);
void basalt_printf_redirect_set_sink(basalt_log_sink_fn sink);

// --- global-namespace wrappers -------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
size_t hali_basalt_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int    hali_basalt_printf(const char *fmt, ...);
int    hali_basalt_fprintf(FILE *stream, const char *fmt, ...);
#ifdef __cplusplus
}
#endif

// --- std-namespace wrappers (basalt uses std::fwrite, std::printf, …) ----
#ifdef __cplusplus
namespace std {
  size_t hali_basalt_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
  int    hali_basalt_printf(const char *fmt, ...);
  int    hali_basalt_fprintf(FILE *stream, const char *fmt, ...);
}
#endif

// --- the actual rename -----------------------------------------------------
#define fwrite  hali_basalt_fwrite
#define printf  hali_basalt_printf
#define fprintf hali_basalt_fprintf

#endif // BASALT_PRINTF_REDIRECT_H