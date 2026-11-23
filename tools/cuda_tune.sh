#!/usr/bin/env bash
set -euo pipefail
BUILD_DIR="${BUILD_DIR:-build}"
BENCH="${BUILD_DIR}/bin/llama-bench"
NCU="${NCU:-ncu}"
SETTINGS=".hali/settings.json"
RESULTS_DIR=".hali/tuning"
PROMPT_LEN="${PROMPT_LEN:-512}"
GEN_LEN="${GEN_LEN:-128}"
WARMUP="${WARMUP:-3}"

get_setting() {
    local key="$1" default="${2:-}"
    if command -v jq &>/dev/null; then
        jq -r ".$key // empty" "$SETTINGS" 2>/dev/null || echo "$default"
    elif command -v python3 &>/dev/null; then
        python3 -c "
import json
d=json.load(open('$SETTINGS'))
v=d
for k in '$key'.split('.'): v=v.get(k,'') if isinstance(v,dict) else ''
print(v if v else '$default')" 2>/dev/null || echo "$default"
    else
        echo "$default"
    fi
}

MODEL_PATH="$(get_setting 'model.path' '')"
MODEL_NAME="$(basename "${MODEL_PATH:-unknown}" .gguf)"
N_CTX="$(get_setting 'model.n_ctx' '65536')"

[[ -n "$MODEL_PATH" && -f "$MODEL_PATH" ]] || {
    echo "ERROR: model not found: ${MODEL_PATH:-<empty>}" >&2
    exit 1
}

ts() { date '+%Y%m%d_%H%M%S'; }
ensure_dir() { mkdir -p "$RESULTS_DIR"; }

check_deps() {
    [[ -x "$BENCH" ]] || {
        echo "ERROR: ${BENCH} not found. Run: $0 setup" >&2; exit 1; }
    command -v "$NCU" &>/dev/null || echo "WARNING: ncu not found" >&2
}

parse_bench() {
    local f="$1" p d
    p=$(grep -oP '[0-9.]+\s+t/s' "$f" | head -1 | grep -oP '[0-9.]+' || echo "N/A")
    d=$(grep -oP '[0-9.]+\s+t/s' "$f" | tail -1 | grep -oP '[0-9.]+' || echo "N/A")
    echo "$p $d"
}

write_header() {
    local f="$1"
    {
        echo "# CUDA Tuning Report"
        echo ""
        echo "**Generated:** $(date '+%Y-%m-%d %H:%M:%S')"
        echo "**Model:** ${MODEL_PATH}"
        echo "**Name:** ${MODEL_NAME}"
        echo "**n_ctx:** ${N_CTX}"
        echo "**Prompt:** ${PROMPT_LEN} **Gen:** ${GEN_LEN} **Warmup:** ${WARMUP}"
        echo ""
    } > "$f"
}

append_bench() {
    local f="$1" label="$2" out="$3"
    local p d
    read -r p d < <(parse_bench "$out")
    {
        echo "## ${label}"
        echo ""
        echo "| Metric | Value |"
        echo "|--------|-------|"
        echo "| Prefill t/s | ${p} |"
        echo "| Decode t/s | ${d} |"
        echo ""
        echo '```'
        cat "$out"
        echo '```'
        echo ""
    } >> "$f"
}

append_compare() {
    local f="$1" dir="$2"
    {
        echo "## Comparison"
        echo ""
        echo "| Label | Prefill t/s | Decode t/s |"
        echo "|-------|-------------|------------|"
        for bf in "${dir}"/bench_*.txt; do
            [[ -f "$bf" ]] || continue
            local l p d
            l=$(basename "$bf" .txt | sed 's/^bench_//')
            read -r p d < <(parse_bench "$bf")
            echo "| ${l} | ${p} | ${d} |"
        done
        echo ""
    } >> "$f"
}

cmd_setup() {
    echo "=== Build Setup: Blackwell sm_120 ==="
    command -v nvcc &>/dev/null || { echo "ERROR: nvcc not found" >&2; exit 1; }
    echo "    CUDA: $(nvcc --version | grep -oP '\d+\.\d+' | tail -1)"
    command -v nvidia-smi &>/dev/null && \
        echo "    GPU:  $(nvidia-smi --query-gpu=name,compute_cap --format=csv,noheader 2>/dev/null | head -1)"
    echo "    Arch: 120a-real"
    echo ""
    echo "Configuring..."
    cmake -B "$BUILD_DIR" \
        -DCMAKE_CUDA_ARCHITECTURES=120a-real \
        -DGGML_CUDA_FORCE_MMQ=ON \
        -DGGML_CUDA_FA=ON \
        -DCMAKE_BUILD_TYPE=Release . 2>&1 | tail -5
    echo "Building..."
    cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -10
    [[ -x "$BENCH" ]] && echo "OK: ${BENCH}" || { echo "ERROR: build failed" >&2; exit 1; }
}

cmd_rebuild() {
    [[ -d "$BUILD_DIR" ]] || { echo "No build dir. Run: $0 setup" >&2; exit 1; }
    cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -10
    [[ -x "$BENCH" ]] && echo "OK: ${BENCH}" || { echo "ERROR: ${BENCH} missing" >&2; exit 1; }
}

cmd_baseline() {
    check_deps; ensure_dir
    local t l o r
    t="$(ts)"; l="baseline_${t}"; o="${RESULTS_DIR}/bench_${l}.txt"; r="${RESULTS_DIR}/report_${t}.md"
    echo "=== Baseline: ${MODEL_NAME} ==="
    "$BENCH" -m "$MODEL_PATH" -p "$PROMPT_LEN" -n "$GEN_LEN" -r "$WARMUP" 2>&1 | tee "$o"
    write_header "$r"
    append_bench "$r" "$l" "$o"
    echo "Report: $r"
}

cmd_profile() {
    check_deps; ensure_dir
    command -v "$NCU" &>/dev/null || { echo "ncu not available" >&2; exit 1; }
    local t r
    t="$(ts)"; r="${RESULTS_DIR}/report_profile_${t}.md"
    write_header "$r"
    for pair in "ggml_cuda_mmq:mmq" "ggml_cuda_mmvq:mmvq" "ggml_cuda_fattn:fattn"; do
        local kr lbl nf
        kr="${pair%%:*}"; lbl="${pair##*:}"; nf="${RESULTS_DIR}/profile_${lbl}.ncu-rep"
        echo "  Profiling: ${lbl}"
        "$NCU" --set full --kernel-name "regex:${kr}" \
            --export "${nf%.ncu-rep}" --force-overwrite \
            "$BENCH" -m "$MODEL_PATH" -p "$PROMPT_LEN" -n 1 2>&1 | tail -3
        {
            echo "## Profile: ${lbl}"
            echo ""
            echo "- File: ${nf}"
            echo "- Open: ncu --input ${nf}"
            echo ""
        } >> "$r"
    done
    echo "Report: $r"
}

cmd_sweep() {
    local q="${1:-Q4_0}"
    check_deps; ensure_dir
    local t l o r
    t="$(ts)"; l="sweep_${q}_${t}"; o="${RESULTS_DIR}/bench_${l}.txt"; r="${RESULTS_DIR}/report_sweep_${t}.md"
    echo "=== Sweep: ${q} (current build) ==="
    "$BENCH" -m "$MODEL_PATH" -p "$PROMPT_LEN" -n "$GEN_LEN" -r "$WARMUP" 2>&1 | tee "$o"
    write_header "$r"
    append_bench "$r" "$l" "$o"
    append_compare "$r" "$RESULTS_DIR"
    echo "Report: $r"
    echo "Next: edit config, rebuild, re-run sweep."
}

cmd_compare() {
    ensure_dir
    local r
    r="${RESULTS_DIR}/report_compare_$(ts).md"
    write_header "$r"
    append_compare "$r" "$RESULTS_DIR"
    cat "$r"
}

cmd_report() {
    local latest
    latest=$(ls -t "${RESULTS_DIR}"/report_*.md 2>/dev/null | head -1)
    [[ -n "$latest" ]] || { echo "No reports in ${RESULTS_DIR}/" >&2; exit 1; }
    cat "$latest"
}

case "${1:-help}" in
    setup)    cmd_setup ;;
    rebuild)  cmd_rebuild ;;
    baseline) cmd_baseline ;;
    profile)  cmd_profile ;;
    sweep)    cmd_sweep "${2:-Q4_0}" ;;
    compare)  cmd_compare ;;
    report)   cmd_report ;;
    *)
        echo "cuda_tune.sh — Blackwell sm_120 tuning harness"
        echo ""
        echo "  $0 setup              Configure + build (120a-real, MMQ, FA)"
        echo "  $0 rebuild            Rebuild after config changes"
        echo "  $0 baseline           Benchmark current build"
        echo "  $0 profile            Nsight Compute profiles"
        echo "  $0 sweep [quant]      Record benchmark for sweep"
        echo "  $0 compare            Side-by-side results"
        echo "  $0 report             Show latest report"
        echo ""
        echo "Model: $SETTINGS (model.path)"
        echo "Reports: $RESULTS_DIR/"
        ;;
esac