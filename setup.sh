#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  H A L I  ·  Haliastur  ·  Hardware-Aware Local Inference
#  setup.sh — submodules, deps, build, test.  One shot.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# ── colors ───────────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
  R=$'\033[0m'  B=$'\033[1m'  D=$'\033[2m'
  RED=$'\033[31m' GRN=$'\033[32m' YLW=$'\033[33m'
  BLU=$'\033[34m' MAG=$'\033[35m' CYN=$'\033[36m' WHT=$'\033[37m'
else
  R="" B="" D="" RED="" GRN="" YLW="" BLU="" MAG="" CYN="" WHT=""
fi

# ── mascot ───────────────────────────────────────────────────────────────────
mascot() {
  echo
  echo "${MAG}     ▄▅▆░██▄▅   ▄▅▆░██▄▅   ▄▅▆█░█░█▆▅▄   ▅▆░██▄▅  ${R}"
  echo "${CYN}        ▄▅█ ◉ █▄ ▄▅█ ◉ █▄  ▄▅█ ◕ ◕ █▄   ▄▅█ ◉ █▄  ${R}"
  echo "${GRN}      ▄▅░██░██▄▅ ▄▅░██░██▄▅ ▄▅░██░██▄▅ ▄▅░██░██▄▅ ${R}"
  echo "${YLW}      ▀▄█░█▓▄   ▀▄█░█▓▄      ▀▄█ ░█▓▄    ▓▓▓      ${R}"
  echo
  echo "${D}  ─────────── agentic LLM shell · setup ───────────${R}"
  echo
}

# ── helpers ──────────────────────────────────────────────────────────────────
step()  { echo "${B}${CYN}▶ ${1}${R}"; }
ok()    { echo "  ${GRN}✓ ${1}${R}"; }
warn()  { echo "  ${YLW}⚠ ${1}${R}"; }
fail()  { echo "  ${RED}✗ ${1}${R}"; exit 1; }
have()  { command -v "$1" &>/dev/null; }

# ── main ─────────────────────────────────────────────────────────────────────
mascot

# 1. git submodules
step "git submodules"
if ! have git; then
  fail "git not found — install git first"
fi
git submodule update --init --recursive
ok "submodules ready (llama.cpp, yyjson)"

# 2. build deps
step "checking dependencies"
for cmd in cmake g++ make; do
  if ! have "$cmd"; then
    fail "$cmd not found — install it (e.g. apt install $cmd)"
  fi
done
ok "cmake, g++, make"

# notcurses (required)
if pkg-config --exists notcurses 2>/dev/null; then
  ok "notcurses (pkg-config)"
elif [[ -f /usr/include/notcurses/notcurses.h ]]; then
  ok "notcurses (system headers)"
else
  warn "notcurses not found — try: apt install libnotcurses-dev"
  warn "  or pass -DNOTCURSES_DIR=<prefix> to cmake"
fi

# libcurl (optional)
if pkg-config --exists libcurl 2>/dev/null; then
  ok "libcurl (pkg-config) — TOOL:CURL enabled"
else
  warn "libcurl not found — TOOL:CURL will be disabled"
  warn "  install with: apt install libcurl4-openssl-dev"
fi

# 3. build
step "building (LLAMA_BACKEND=${LLAMA_BACKEND:-AUTO})"
cmake -B build -DLLAMA_BACKEND="${LLAMA_BACKEND:-AUTO}"
cmake --build build -j"$(nproc)"
ok "build complete → build/bin/hali"

# 4. smoke test
step "smoke test"
if [[ -x build/bin/hali ]]; then
  build/bin/hali --help &>/dev/null
  ok "hali --help ran successfully"
else
  warn "binary not found at build/bin/hali — skipping"
fi

# 5. unit tests (optional)
if [[ "${1:-}" == "--test" ]]; then
  step "running unit tests"
  cmake -B build-tests -S tests
  cmake --build build-tests -j"$(nproc)"
  ctest --test-dir build-tests --output-on-failure
  ok "tests done"
fi

echo
echo "${B}${GRN}  ✨ all done — run ./build/bin/hali to take flight${R}"
echo
