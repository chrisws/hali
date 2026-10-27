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

# 5. GPU probe → model selection
step "detecting GPU VRAM"
VRAM_MB=""
PROBE_BIN="/tmp/hali_gpu_probe"

# Try compiling the CUDA probe
if have nvcc; then
  if nvcc -o "${PROBE_BIN}" tools/gpu_probe.c -lcudart 2>/dev/null; then
    VRAM_MB=$("${PROBE_BIN}" 2>/dev/null || true)
  fi
fi

# Fallback: nvidia-smi
if [[ -z "${VRAM_MB}" ]] && have nvidia-smi; then
  VRAM_MB=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null | head -1 | tr -d ' ')
fi

# Pick model tier
if [[ -n "${VRAM_MB}" ]]; then
  ok "GPU detected: ${VRAM_MB} MB VRAM"
  if (( VRAM_MB >= 32768 )); then
    MODEL_NAME="qwen3-32b-q4_k_m.gguf"
    MODEL_URL="https://huggingface.co/Qwen/Qwen3-32B-GGUF/resolve/main/${MODEL_NAME}"
    MODEL_SIZE="~20 GB"
    N_CTX=32768
    KV_PRESET="f16"
  elif (( VRAM_MB >= 16384 )); then
    MODEL_NAME="qwen3-14b-q4_k_m.gguf"
    MODEL_URL="https://huggingface.co/Qwen/Qwen3-14B-GGUF/resolve/main/${MODEL_NAME}"
    MODEL_SIZE="~9 GB"
    N_CTX=16384
    KV_PRESET="f16"
  elif (( VRAM_MB >= 8192 )); then
    MODEL_NAME="qwen3-8b-q4_k_m.gguf"
    MODEL_URL="https://huggingface.co/Qwen/Qwen3-8B-GGUF/resolve/main/${MODEL_NAME}"
    MODEL_SIZE="~5.5 GB"
    N_CTX=16384
    KV_PRESET="q8_0"
  else
    MODEL_NAME="qwen3-4b-q4_k_m.gguf"
    MODEL_URL="https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/${MODEL_NAME}"
    MODEL_SIZE="~2.6 GB"
    N_CTX=16384
    KV_PRESET="f16"
  fi
else
  warn "no GPU detected — defaulting to Qwen3-4B (CPU)"
  MODEL_NAME="qwen3-4b-q4_k_m.gguf"
  MODEL_URL="https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/${MODEL_NAME}"
  MODEL_SIZE="~2.6 GB"
  N_CTX=16384
  KV_PRESET="f16"
fi

step "downloading model (${MODEL_NAME})"
MODEL_DIR="models"
MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"

mkdir -p "${MODEL_DIR}"

if [[ -f "${MODEL_PATH}" ]]; then
  ok "model already exists: ${MODEL_PATH}"
else
  if ! have curl; then
    fail "curl not found — install it (e.g. apt install curl)"
  fi
  echo "  downloading ${MODEL_SIZE} from HuggingFace…"
  curl -L --progress-bar -o "${MODEL_PATH}" "${MODEL_URL}"
  # sanity check: file should be > 1 GB
  FILE_SIZE=$(stat -c%s "${MODEL_PATH}" 2>/dev/null || echo 0)
  if (( FILE_SIZE < 1073741824 )); then
    fail "downloaded file is only $(( FILE_SIZE / 1048576 )) MB — likely incomplete"
  fi
  ok "model saved → ${MODEL_PATH}"
fi

# 6. create .hali.config.json
step "creating .hali.config.json"
CONFIG_FILE=".hali.config.json"
cat > "${CONFIG_FILE}" <<JSON
{
  "model": {
    "path": "models/${MODEL_NAME}",
    "kv_cache_preset": "${KV_PRESET}",
    "n_ctx": ${N_CTX},
    "n_batch": 512,
    "n_gpu_layers": 64,
    "n_threads": 8,
    "n_threads_batch": 8,
    "temperature": 0.6,
    "top_p": 0.95,
    "min_p": 0.0,
    "top_k": 20,
    "penalty_repeat": 1.0,
    "penalty_freq": 0.0,
    "penalty_present": 0.0,
    "penalty_last_n": 256,
    "offload_kqv": true,
    "rope_scaling": "none",
    "rope_freq_scale": 0.0
  },
  "rag": {
    "embed_path": "",
    "top_k": 5
  },
  "mtp": {
    "enabled": false,
    "n_max": 3,
    "n_min": 1,
    "p_min": 0.0,
    "pos_threshold": 0.4,
    "adaptive": true
  },
  "suffix": {
    "enabled": false,
    "ngram": 4,
    "max_draft": 2
  },
  "audio": {
    "enabled": false,
    "volume": 0.5,
    "tool_tick": false,
    "user_prompt": false
  },
  "mcp": {
    "host": "127.0.0.1",
    "port": 64342
  },
  "logging": {
    "level": "info",
    "file": "text.log",
    "filter": "text",
    "erase": true
  },
  "permission_prompt": false,
  "disable_run": false,
  "disable_save_on_exit": false,
  "validation": {
    "disable_write_validate": true,
    "disable_patch_validate": true
  },
  "extra_dirs": [],
  "thinking": true,
  "web_port": -1,
  "enabled_backups": false,
  "knowledge_files": [],
  "mcp_filter": [],
  "val_write_size_warn": 4000
}
JSON
ok "config written → ${CONFIG_FILE}"

# 7. unit tests (optional)
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
