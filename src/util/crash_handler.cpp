//
// Crash handler implementation.
//
#include "util/crash_handler.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

#include "ui/tui.h"

namespace {

//
// Signal handler: print backtrace, then re-raise with default handler.
//
void crash_handler(int sig, siginfo_t *info, void *ucontext) {
  (void)info;
  (void)ucontext;

  // Restore terminal state (notcurses) before printing crash info.
  // Not strictly async-signal-safe, but the process is dying anyway —
  // a broken terminal is worse than a best-effort cleanup.
  Tui::cleanup();

  const char *msg = "\n\033[1;31m=== HALI CRASH ===\033[0m\n";
  ssize_t n = write(2, msg, sizeof(msg) - 1);
  (void)n;

  // Generate backtrace.
  void *frames[64];
  int count = backtrace(frames, 64);
  char **symbols = backtrace_symbols(frames, count);

  // Write each symbol line to stderr (async-signal-safe).
  if (symbols) {
    for (int i = 0; i < count; ++i) {
      n = write(2, symbols[i], __builtin_strlen(symbols[i]));
      n = write(2, "\n", 1);
      (void)n;
    }
    free(symbols);
  }

  const char *end = "\n=== END CRASH ===\n";
  n = write(2, end, sizeof(end) - 1);
  (void)n;

  // Restore default handler and re-raise so core dump fires.
  signal(sig, SIG_DFL);
  raise(sig);
}

}  // namespace

void crash_handler_install() {
  struct sigaction sa {};
  sa.sa_sigaction = crash_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGFPE, &sa, nullptr);
  sigaction(SIGBUS, &sa, nullptr);
}

