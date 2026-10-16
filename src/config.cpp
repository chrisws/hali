// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <string>
#include <fstream>
#include <format>
#include <sstream>

#include "config.h"
#include "json.h"
#include "gpu_info.h"

static const std::vector<std::string> ALLOWED_TOOLS = {
  "wc", "stat", "tr", "cut", "sort", "uniq", "od", "xxd", "git",
  "file", "uname", "whoami", "pwd", "id", "g++", "date", "make"
};

static KVCachePreset to_kv_preset(const std::string &str) {
  if (str == "f16") {
    return KVCachePreset::F16;
  } else if (str == "balanced") {
    return KVCachePreset::Balanced;
  }
  return KVCachePreset::Compact;
}

HaliConfig::HaliConfig() {
  sandbox_ = fs::current_path().string();
  for (const auto &tool : ALLOWED_TOOLS) {
    run_allowed_.emplace_back(tool);
  }
}

std::string HaliConfig::kv_preset_to_string() const {
  switch (kv_preset_) {
    case KVCachePreset::F16:      return "f16";
    case KVCachePreset::Balanced: return "balanced";
    case KVCachePreset::Compact:  return "compact";
  }
  return "compact";
}

//
// Settings persistence  (~/.config/hali/hali.settings.json)
// Returns the canonical settings path: ~/.config/hali/settings.json
//
std::string HaliConfig::settings_path() const {
  // Attempt to read settings from the current working directory first
  if (fs::exists(config_)) {
    return config_;
  }
  const char *home = getenv("HOME");
  std::string base = home ? std::string(home) : ".";
  return base + "/.config/hali/settings.json";
}

void HaliConfig::set_config(const std::string &config) {
  if (fs::exists(config)) {
    config_ = config;
  }
}

// Load settings from disk into cfg.  Fields present in the file overwrite
// the defaults already in cfg; fields absent are left at their defaults.
// Silently succeeds if the file doesn't exist yet.
void HaliConfig::load_settings() {
  std::string path = settings_path();
  std::ifstream f(path);
  if (!f) return;                  // no file → use defaults
  std::ostringstream oss; oss << f.rdbuf();
  std::string json = oss.str();

  auto doc = json::parse(json);
  if (!doc.is_valid()) {
    return;
  }

  auto root = doc.get_root();

  // ── model section ──────────────────────────────────────────────
  auto model = root.get_child("model");
  if (model.is_object()) {
    model.get_str("path", model_path_);
    std::string kv_preset;
    model.get_str("kv_cache_preset", kv_preset);
    kv_preset_ = to_kv_preset(kv_preset);
    model.get_int("n_ctx", n_ctx_);
    model.get_int("n_batch", n_batch_);
    model.get_int("n_gpu_layers", n_gpu_layers_);
    model.get_int("top_k", top_k_);
    model.get_int("penalty_last_n", penalty_last_n_);
    model.get_int("n_threads", n_threads_);
    model.get_int("n_threads_batch", n_threads_batch_);
    model.get_float("temperature", temperature_);
    model.get_float("top_p", top_p_);
    model.get_float("min_p", min_p_);
    model.get_float("penalty_repeat", penalty_repeat_);
    model.get_float("penalty_freq", penalty_freq_);
    model.get_float("penalty_present", penalty_present_);
    model.get_bool("offload_kqv", offload_kqv_);
  }

  // ── rag section ────────────────────────────────────────────────
  auto rag = root.get_child("rag");
  if (rag.is_object()) {
    rag.get_str("embed_path", embed_path_);
    rag.get_int("top_k", rag_top_k_);
  }

  // ── mtp section ────────────────────────────────────────────────
  auto mtp = root.get_child("mtp");
  if (mtp.is_object()) {
    mtp.get_bool("enabled", mtp_enabled_);
    mtp.get_int("n_max", mtp_n_max_);
    mtp.get_int("n_min", mtp_n_min_);
    mtp.get_float("p_min", mtp_p_min_);
    mtp.get_float("pos_threshold", mtp_pos_threshold_);
    mtp.get_bool("adaptive", mtp_adaptive_);
  }

  // ── suffix section ─────────────────────────────────────────────
  auto suffix = root.get_child("suffix");
  if (suffix.is_object()) {
    suffix.get_bool("enabled", suffix_enabled_);
    suffix.get_int("ngram", suffix_ngram_);
    suffix.get_int("max_draft", suffix_max_draft_);
  }

  // ── audio section ──────────────────────────────────────────────
  auto audio = root.get_child("audio");
  if (audio.is_object()) {
    audio.get_bool("enabled", audio_enabled_);
    audio.get_float("volume", audio_volume_);
    audio.get_bool("tool_tick", audio_tool_tick_);
    audio.get_bool("user_prompt", audio_user_prompt_);
  }

  // ── mcp section (plan 12) ──────────────────────────────────────
  auto mcp = root.get_child("mcp");
  if (mcp.is_object()) {
    mcp.get_str("host", mcp_host_);
    mcp.get_int("port", mcp_port_);
  }

  // ── logging section (plan 12) ──────────────────────────────────
  auto logging = root.get_child("logging");
  if (logging.is_object()) {
    logging.get_str("level", log_level_str_);
    logging.get_str("file", log_file_);
    logging.get_str("filter", log_filter_);
  }
}

// Persist the current cfg to ~/.config/hali/settings.json.
bool HaliConfig::save_settings() const {
  std::string path = settings_path();
  fs::path dir = fs::path(path).parent_path();
  std::error_code ec;
  fs::create_directories(dir, ec);

  std::ofstream f(path, std::ios::trunc);
  if (!f) {
    return false;
  }

  f << introspect();

  return f.good();
}

//
// System prompt
//
std::string HaliConfig::build_system_prompt() const {
  std::string p;
  int kv_threshold = gpu_info::kv_restart_threshold();
  p +=
    "You are Hali, an agentic AI assistant for software development. "
    "Proceed with caution, guided by logic and the pursuit of knowledge.\n\n"

    "Your sandbox (project directory) is: " + sandbox_ + "\n\n"

    "## Core Principle\n"
    "Always follow this loop: THINK → DECIDE → ACT → RESPOND\n\n"

    "## Reasoning Protocol\n"
    "Use <|think|> to reason BEFORE acting. Keep it concise and structured.\n"
    "Format:\n"
    "<|think|>\n"
    "- What is the user asking?\n"
    "- Do I need external data (files, tools)?\n"
    "- What is the safest and most correct action?\n"
    "</|think|>\n\n"

    "Rules:\n"
    "- Do NOT call tools inside <|think|>\n"
    "- Do NOT include the final answer inside <|think|>\n"
    "- Always follow <|think|> with either a tool call OR a final answer\n"
    "- Skip <|think|> only for trivial or conversational responses\n"
    "- Use ASK when user intent is unclear or missing critical parameters\n"
    "- Query RAG when user asks about project-specific knowledge or when uncertain\n\n"

    "## Execution Model\n"
    "- Single-threaded: Only ONE tool call can be active at a time\n"
    "- Sequential execution: Wait for HALI_TOOL_RESULT before issuing next tool\n"
    "- No parallel tool calls or batched requests\n\n"

    "## Tool Protocol\n"
    "Emit ONE tool call at a time, immediately followed by HALI_END_TOOL.\n"
    "Do NOT add any commentary, explanation, or text between the tool call and HALI_END_TOOL.\n"
    "The host executes the tool and returns HALI_TOOL_RESULT: <value>.\n"
    "Wait for the result before continuing.\n"
    "After receiving HALI_TOOL_RESULT you may explain what you did.\n\n"
    "Examples:\n\n"
    "TOOL:LIST\n"
    "HALI_END_TOOL\n\n"
    "TOOL:READ readme.txt\n"
    "HALI_END_TOOL\n\n"
    "TOOL:WRITE index.html <!DOCTYPE html><html>...</html>\n"
    "HALI_END_TOOL\n\n"
    "TOOL:RUN ./build.sh\n"
    "HALI_END_TOOL\n\n"

    "## Available Tools\n"
    "  TOOL:LIST   [dir]          list files (default: sandbox root)\n"
    "  TOOL:READ   <file>         read file contents (max 32 KB; use TOOL:SEARCH for larger files)\n"
    "  TOOL:SEARCH <pattern> <path> [flags]  search files (flags: --recursive --line-numbers --count --line-count --files-only --name --include=<glob> --context=<n> --lines=<start>,<end>)\n"
    "  TOOL:APPEND <file> <text>  append text to an existing file\n"
    "  TOOL:PATCH  <file> <tags>  patch an existing file - see below\n"
    "  TOOL:WRITE  <file> <text>  write text to file\n"
    "  TOOL:MKDIR  <dir>          create a subfolder inside the sandbox\n"
    "  TOOL:EXISTS <file>         YES or NO\n"
    "  TOOL:RUN    <prog> [args]  run program inside sandbox\n"
    "  TOOL:BUILD  [mode]         setup/build/test/clean (auto-detects CMake, Gradle, Maven, Python; modes: setup, build, test, clean, default=all)\n"
    "  TOOL:REPLACE <file> <find>|<replace> [flags]  replace text in file (flags: --all, --count)\n"
    "  TOOL:DATE                  current date\n"
    "  TOOL:TIME                  current time\n"
    "  TOOL:RND                   random float 0..1\n"
    "  TOOL:RAG    <query>        query the RAG index for additional context\n"
    "  TOOL:ASK    <query>        ask the user for clarification or additional context\n"
    "  TOOL:GRAPH  <json>         display tree/bar charts.\n"
    "  TOOL:TABLE  <R>x<C> <rows>  format pipe-delimited rows into an aligned table\n"
    "  TOOL:INTROSPECT            show current model settings\n"
    "  TOOL:CURL   <url>          HTTP GET, returns response body (max 32 KB)\n"
    "  TOOL:PERMISSION            ask user for explicit permission\n"
    "  TOOL:RESTART               restart after writing current task context to `SESSION.md`\n\n"

    "## Tool Decision Rules\n"
    "Use tools ONLY if:\n"
    "- The user explicitly references files or the project, OR\n"
    "- The answer depends on local or project data, OR\n"
    "- The user asks for date, time, or a random number\n"
    "Otherwise answer directly using internal knowledge.\n"
    "If the project has a recognisable build system, prefer TOOL:BUILD over\n"
    "TOOL:RUN cmake/make/mvn/gradle. Use TOOL:RUN only for one-off commands\n"
    "not covered by the build system.\n\n"

    "## Tool Rules\n"
    "- HALI_END_TOOL must immediately follow the tool call — no exceptions\n"
    "- Never add commentary before HALI_END_TOOL\n"
    "- Only use one tool at a time, step by step\n"
    "- Never access files outside the sandbox\n"
    "- Use TOOL:PERMISSION before destructive or irreversible operations\n"
    "- Do NOT hallucinate file contents\n"
    "- Do NOT fabricate tool outputs\n"
    "- Do NOT assume files exist — use TOOL:EXISTS to check first\n\n"

    "## SEARCH Tool Notes\n"
    "- `<pattern>` is a single token; use quotes for multi-word: `\"hello world\"`\n"
    "- Flags: --recursive --line-numbers --context=<n> --include=<glob> --files-only --name --lines=<start>,<end>\n"
    "- `--context=<n>` shows n lines before/after each match\n"
    "- `--lines=<start>,<end>` restricts search to a line range (like sed -n)\n"
    "- `--name` matches the pattern against filenames (glob) instead of file content\n\n"

    "## TOOL:GRAPH Rules\n"
    "- For TOOL:GRAPH, Reference `skills/graph.md` when you need the full schema details\n"
    "- Only expand the context when actively working with graphs\n\n"

    "## TOOL:TABLE Rules\n"
    "- ALWAYS use TOOL:TABLE when presenting tabular data (comparisons, columnar lists, summaries with 2+ columns)\n"
    "- Do NOT write markdown tables (| col | col |) in your response text\n"
    "- Format: first line is <R>x<C>, then one pipe-delimited row per line\n"
    "- Example: TOOL:TABLE 3x2 Name|Value\\nfoo|1\\nbar|2\n\n"

    "## File Writing Rules\n"
    "Use TOOL:WRITE only if explicitly requested. Prefer TOOL:APPEND or TOOL:PATCH\n"
    "- Write complete and valid content\n"
    "- Do not overwrite without clear intent\n"
    "- Use TOOL:PERMISSION before overwriting an existing file\n"
    "- Format: TOOL:WRITE <filename> <complete file content>\n\n"

    "## Code style Rules\n"
    "- Format: Strict K&R. 2-space indents, no tabs.\n"
    "- Always use explicit curly braces {} for all control statements, even single-line blocks.\n\n"

    "## File Patching Rules\n"
    "Use TOOL:PATCH to modify an existing file.\n"
    "- **Scope patches at function/block granularity**, not line-diffs\n"
    "  - A \"block\" = complete logical unit (function, class method, if/else branch)\n"
    "  - Never patch in the middle of a function or statement\n"
    "- **Full-rewrite fallback** for small files (<200 lines)\n"
    "  - If the file is small and you're making major changes, rewrite entirely\n"
    "  - This is safer than trying to patch multiple scattered sections\n"
    "- **Always provide the complete function body**\n"
    "  - From function signature to closing brace\n"
    "  - Include all parameters, return type, and documentation\n"
    "  - Never provide a partial range or omit the closing brace\n"
    "- **Patch matching**\n"
    "  - The OLD section must match the existing file exactly\n"
    "  - If the patch doesn't match, the tool will fail\n"
    "  - Consider using TOOL:READ first to verify current content\n"
    "### Potential Ambiguities\n"
    "1. **What if a function spans multiple files?** → Use multiple PATCH calls\n"
    "2. **What if there are multiple functions in one file?** → PATCH each separately\n"
    "3. **What about comments/docstrings?** → Include them in the complete function body\n\n"
    "### Format:\n"
    "TOOL:PATCH filename.cpp\n"
    "<<<<<<< OLD\n"
    "[complete old function]\n"
    "=======\n"
    " [complete new function]\n"
    ">>>>>>> NEW\n"
    "HALI_END_TOOL\n\n"

    "## Interaction Guidelines\n"
    "- Be precise and efficient\n"
    "- Ask clarifying questions if the request is ambiguous or missing parameters\n"
    "- Prefer direct answers when no tools are needed\n"
    "- After each tool result, explain in plain English what was done\n"
    "- Always consider whether to add unit tests for any changes\n"
    "- If no user request is provided, respond with a brief readiness message\n"
    "- If you detect a project with source files but no configured build\n"
    "  directories, offer to run TOOL:BUILD setup rather than waiting.\n"
    "- Do not interpret conversational words like \"now\", \"build\", or \"test\"\n"
    "  as TOOL:BUILD mode arguments. Only emit TOOL:BUILD when the user\n"
    "  explicitly asks to build, test, clean, or set up the project.\n\n"

    "## Auto-Restart Protocol\n"
    "**When:** - When KV >= " + std::to_string(kv_threshold) + "% (as reported in the tool results footer).\n"
    "**Steps:**\n"
    "1. **Save State:** Write current task context to `SESSION.md` using `TOOL:WRITE`.\n"
    "   - Include: Timestamp, KV usage, current task description, plan file (if any), pending actions, and last conversation summary.\n"
    "   - Don't check if SESSION.md already exists from another session. just use TOOL:WRITE.\n"
    "2. **Trigger Restart:** Call `TOOL:RESTART` to start over.\n\n"
    "**Example `SESSION.md` Content:**\n"
    "```markdown\n"
    "# Session State Snapshot\n"
    "**Timestamp:** <date> <time>\n"
    "**Current Task:** <task description>\n"
    "**Plan File:** <plans/...> (or \"none\")\n"
    "**Pending Actions:**\n"
    "- <action 1>\n"
    "- <action 2>\n"
    "**Last Output:**\n"
    "<last few lines of conversation>\n"
    "```\n\n";

  if (audio_enabled_) {
    p +=
      "## Additional Tools\n"
      "  TOOL:PLAY   <notes>         play a melody (space-separated note tokens)\n\n"
      "## TOOL:PLAY Rules\n"
      "- Play melodies using space-separated note tokens.\n"
      "- Notes: letter (A-G) + optional accidental (# or -) + octave digit (0-6) + optional :duration\n"
      "  - C4 = middle C, D#5 = D sharp octave 5, E-3 = E flat octave 3\n"
      "  - C4:0.5 = C4 with explicit 0.5s duration\n"
      "- Control tokens: T<BPM> (tempo, default 120), V<0-100> (volume, default 50), L<1-64> (note length, default 4), P<beats> (pause)\n"
      "- Example: TOOL:PLAY C4 E4 G4 C5 plays a C major arpeggio\n"
      "- Example: TOOL:PLAY T120 V70 C4:0.3 D4:0.3 E4:0.3 P1 C5:0.5\n"
      "- When the user asks you to play a tune, try your best interpretation, then ask for feedback.\n\n";
  }

  p += mcp_context_;

  for (const auto &kf : knowledge_files_) {
    std::ifstream f(kf);
    if (f) {
      std::ostringstream oss; oss << f.rdbuf();
      p += "## Knowledge: " + kf + "\n" + oss.str() + "\n\n";
    }
  }
  return p;
}

std::string HaliConfig::introspect() const {
  static constexpr std::string_view tmpl =
    "{{\n"
    "  \"model\": {{\n"
    "    \"path\":              \"{}\",\n"
    "    \"kv_cache_preset\":   \"{}\",\n"
    "    \"n_ctx\":             {},\n"
    "    \"n_batch\":           {},\n"
    "    \"n_gpu_layers\":      {},\n"
    "    \"n_threads\":         {},\n"
    "    \"n_threads_batch\":   {},\n"
    "    \"temperature\":       {},\n"
    "    \"top_p\":             {},\n"
    "    \"min_p\":             {},\n"
    "    \"top_k\":             {},\n"
    "    \"penalty_repeat\":    {},\n"
    "    \"penalty_freq\":      {},\n"
    "    \"penalty_present\":   {},\n"
    "    \"penalty_last_n\":    {},\n"
    "    \"offload_kqv\":       {}\n"
    "  }},\n"
    "  \"rag\": {{\n"
    "    \"embed_path\":        \"{}\",\n"
    "    \"top_k\":             {}\n"
    "  }},\n"
    "  \"mtp\": {{\n"
    "    \"enabled\":       {},\n"
    "    \"n_max\":         {},\n"
    "    \"n_min\":         {},\n"
    "    \"p_min\":         {},\n"
    "    \"pos_threshold\": {},\n"
    "    \"adaptive\":      {}\n"
    "  }},\n"
    "  \"suffix\": {{\n"
    "    \"enabled\":   {},\n"
    "    \"ngram\":     {},\n"
    "    \"max_draft\": {}\n"
    "  }},\n"
    "  \"audio\": {{\n"
    "    \"enabled\":     {},\n"
    "    \"volume\":      {},\n"
    "    \"tool_tick\":   {},\n"
    "    \"user_prompt\": {}\n"
    "  }},\n"
    "  \"mcp\": {{\n"
    "    \"host\": \"{}\",\n"
    "    \"port\": {}\n"
    "  }},\n"
    "  \"logging\": {{\n"
    "    \"level\":  \"{}\",\n"
    "    \"file\":   \"{}\",\n"
    "    \"filter\": \"{}\"\n"
    "  }}\n"
    "}}\n";
  return std::format(tmpl,
                     // model
                     model_path_,
                     kv_preset_to_string(),
                     n_ctx_,
                     n_batch_,
                     n_gpu_layers_,
                     n_threads_,
                     n_threads_batch_,
                     temperature_,
                     top_p_,
                     min_p_,
                     top_k_,
                     penalty_repeat_,
                     penalty_freq_,
                     penalty_present_,
                     penalty_last_n_,
                     offload_kqv_,
                     // rag
                     embed_path_,
                     rag_top_k_,
                     // mtp
                     mtp_enabled_,
                     mtp_n_max_,
                     mtp_n_min_,
                     mtp_p_min_,
                     mtp_pos_threshold_,
                     mtp_adaptive_,
                     // suffix
                     suffix_enabled_,
                     suffix_ngram_,
                     suffix_max_draft_,
                     // audio
                     audio_enabled_,
                     audio_volume_,
                     audio_tool_tick_,
                     audio_user_prompt_,
                     // mcp
                     mcp_host_,
                     mcp_port_,
                     // logging
                     log_level_str_,
                     log_file_,
                     log_filter_);
}


//
// /set command handler (plan 12)
// Returns 0=ok, 1=unknown key, 2=parse error
//
int HaliConfig::set_value(const std::string &key, const std::string &val,
                          bool &needs_reparam, std::string &msg) {
  needs_reparam = false;
  msg.clear();
  try {
    if (key == "temperature") {
      temperature_ = std::stof(val); needs_reparam = true;
    } else if (key == "top_p") {
      top_p_ = std::stof(val); needs_reparam = true;
    } else if (key == "min_p") {
      min_p_ = std::stof(val); needs_reparam = true;
    } else if (key == "top_k") {
      top_k_ = std::stoi(val); needs_reparam = true;
    } else if (key == "penalty_repeat") {
      penalty_repeat_ = std::stof(val); needs_reparam = true;
    } else if (key == "penalty_last_n") {
      penalty_last_n_ = std::stoi(val); needs_reparam = true;
    } else if (key == "rag_top_k") {
      rag_top_k_ = std::stoi(val);
    } else if (key == "offload_kqv") {
      offload_kqv_ = (val == "true");
    } else if (key == "n_gpu_layers") {
      n_gpu_layers_ = std::stoi(val);
      msg = "n_gpu_layers will take effect on next /model load.";
    } else if (key == "run_allowed") {
      run_allowed_.clear();
      if (val != "none") {
        std::istringstream iss(val);
        std::string tok;
        while (std::getline(iss, tok, ',')) {
          tok.erase(0, tok.find_first_not_of(" \t"));
          tok.erase(tok.find_last_not_of(" \t") + 1);
          if (!tok.empty()) {
            run_allowed_.push_back(tok);
          }
        }
      }
      if (run_allowed_.empty()) {
        msg = "run_allowed cleared — all sandbox programs permitted.";
      } else {
        std::string list;
        for (const auto &e : run_allowed_) {
          list += e + " ";
        }
        msg = "run_allowed: " + list;
      }
    } else {
      return 1;
    }
  } catch (const std::exception &ex) {
    msg = ex.what();
    return 2;
  }
  return 0;
}