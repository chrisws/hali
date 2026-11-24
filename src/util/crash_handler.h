//
// Crash handler - installs signal handlers that print a backtrace
// on fatal signals (SIGSEGV, SIGABRT, SIGFPE, SIGBUS).
//
#pragma once

//
// Install signal handlers for fatal signals.
// On crash, prints a backtrace to stderr, then re-raises the
// signal so the default handler (core dump) still fires.
//
void crash_handler_install();

