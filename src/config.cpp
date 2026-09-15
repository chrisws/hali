// This file is part of Nitro
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

static const std::vector<std::string> ALLOWED_TOOLS = {
  "cat", "head", "tail", "grep", "wc", "stat", "ls", "find",
  "awk", "sed", "tr", "cut", "sort", "uniq", "od", "xxd",
  "file", "uname", "whoami", "pwd", "id", "g++", "date", "sed"
};

static const KVCachePreset to_kv_preset(const std::string &str) {
  if (str == "f16") {
    return KVCachePreset::F16;
  } else if (str == "balanced") {
    return KVCachePreset::Balanced;
  }
  return KVCachePreset::Compact;
}

NitroConfig::NitroConfig() {
  for (const auto &tool : ALLOWED_TOOLS) {
    run_allowed_.emplace_back(tool);
  }
}

std::string NitroConfig::kv_preset_to_string() const {
  switch (kv_preset_) {
    case KVCachePreset::F16:      return "f16";
    case KVCachePreset::Balanced: return "balanced";
    case KVCachePreset::Compact:  return "compact";
  }
  return "compact";
}

//
// Settings persistence  (~/.config/nitro/nitro.settings.json)
// Returns the canonical settings path: ~/.config/nitro/settings.json
//
std::string NitroConfig::settings_path() const {
  // Attempt to read settings from the current working directory first
  if (fs::exists(config_)) {
    return config_;
  }
  const char *home = getenv("HOME");
  std::string base = home ? std::string(home) : ".";
  return base + "/.config/nitro/settings.json";
}

void NitroConfig::set_config(std::string config) {
  if (fs::exists(config)) {
    config_ = config;
  }
}

// Load settings from disk into cfg.  Fields present in the file overwrite
// the defaults already in cfg; fields absent are left at their defaults.
// Silently succeeds if the file doesn't exist yet.
void NitroConfig::load_settings() {
  std::string path = settings_path();
  std::ifstream f(path);
  if (!f) return;                  // no file → use defaults
  std::ostringstream oss; oss << f.rdbuf();
  std::string json = oss.str();

  // Parse JSON using json::JsonMutDoc
  auto doc = json::parse(json);
  if (!doc.is_valid()) {
    return;
  }

  auto root = doc.get_root();

  // String fields
  root.get_str("model_path", model_path_);
  root.get_str("embed_path", embed_path_);
  root.get_str("sandbox", sandbox_);

  std::string kv_preset;
  root.get_str("kv_cache_preset", kv_preset);
  kv_preset_ = to_kv_preset(kv_preset);

  // Integer fields
  root.get_int("n_ctx", n_ctx_);
  root.get_int("n_batch", n_batch_);
  root.get_int("n_gpu_layers", n_gpu_layers_);
  root.get_int("top_k", top_k_);
  root.get_int("penalty_last_n", penalty_last_n_);
  root.get_int("rag_top_k", rag_top_k_);

  // Float fields
  root.get_float("temperature", temperature_);
  root.get_float("top_p", top_p_);
  root.get_float("min_p", min_p_);
  root.get_float("penalty_repeat", penalty_repeat_);
  root.get_float("penalty_freq", penalty_freq_);
  root.get_float("penalty_present", penalty_present_);

  // bool fields
  root.get_bool("offload_kqv", offload_kqv_);
}

// Persist the current cfg to ~/.config/nitro/settings.json.
bool NitroConfig::save_settings() const {
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
std::string NitroConfig::build_system_prompt() const {
  std::string p;
  p +=
    "You are Nitro, an agentic AI assistant for software development. "
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
    "- Sequential execution: Wait for NITRO_TOOL_RESULT before issuing next tool\n"
    "- No parallel tool calls or batched requests\n\n"

    "## Tool Protocol\n"
    "Emit ONE tool call at a time, immediately followed by NITRO_END_TOOL.\n"
    "Do NOT add any commentary, explanation, or text between the tool call and NITRO_END_TOOL.\n"
    "The host executes the tool and returns NITRO_TOOL_RESULT: <value>.\n"
    "Wait for the result before continuing.\n"
    "After receiving NITRO_TOOL_RESULT you may explain what you did.\n\n"
    "Examples:\n\n"
    "TOOL:LIST\n"
    "NITRO_END_TOOL\n\n"
    "TOOL:READ readme.txt\n"
    "NITRO_END_TOOL\n\n"
    "TOOL:WRITE index.html <!DOCTYPE html><html>...</html>\n"
    "NITRO_END_TOOL\n\n"
    "TOOL:RUN ./build.sh\n"
    "NITRO_END_TOOL\n\n"

    "## Available Tools\n"
    "  TOOL:LIST   [dir]          list files (default: sandbox root)\n"
    "  TOOL:READ   <file>         read file contents\n"
    "  TOOL:APPEND <file> <text>  append text to an existing file\n"
    "  TOOL:PATCH  <file> <tags>  patch an existing file - see below\n"
    "  TOOL:WRITE  <file> <text>  write text to file\n"
    "  TOOL:MKDIR  <dir>          create a subfolder inside the sandbox\n"
    "  TOOL:EXISTS <file>         YES or NO\n"
    "  TOOL:RUN    <prog> [args]  run program inside sandbox\n"
    "  TOOL:DATE                  current date\n"
    "  TOOL:TIME                  current time\n"
    "  TOOL:RND                   random float 0..1\n"
    "  TOOL:RAG    <query>        query the RAG index for additional context\n"
    "  TOOL:ASK    <query>        ask the user for clarification or additional context\n"
    "  TOOL:GRAPH  <json>         display tree/bar charts.\n"
    "  TOOL:INTROSPECT            show current model settings\n"
    "  TOOL:CURL   <url>          HTTP GET, returns response body (max 32 KB)\n"
    "  TOOL:PERMISSION            ask user for explicit permission\n"
    "  TOOL:RESTART               restart after writing current task context to `SESSION.md`\n\n"

    "## Tool Decision Rules\n"
    "Use tools ONLY if:\n"
    "- The user explicitly references files or the project, OR\n"
    "- The answer depends on local or project data, OR\n"
    "- The user asks for date, time, or a random number\n"
    "Otherwise answer directly using internal knowledge.\n\n"

    "## Tool Rules\n"
    "- NITRO_END_TOOL must immediately follow the tool call — no exceptions\n"
    "- Never add commentary before NITRO_END_TOOL\n"
    "- Only use one tool at a time, step by step\n"
    "- Never access files outside the sandbox\n"
    "- Use TOOL:PERMISSION before destructive or irreversible operations\n"
    "- Do NOT hallucinate file contents\n"
    "- Do NOT fabricate tool outputs\n"
    "- Do NOT assume files exist — use TOOL:EXISTS to check first\n\n"

    "## TOOL:GRAPH Rules\n"
    "- For TOOL:GRAPH, Reference `skills/graph.md` when you need the full schema details\n"
    "- Only expand the context when actively working with graphs\n\n"

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
    " TOOL:PATCH filename.cpp\n"
    " <<<<<<< OLD\n"
    " [complete old function]\n"
    " =======\n"
    " [complete new function]\n"
    " >>>>>>> NEW\n"
    " NITRO_END_TOOL\n\n"

    "## Interaction Guidelines\n"
    "- Be precise and efficient\n"
    "- Ask clarifying questions if the request is ambiguous or missing parameters\n"
    "- Prefer direct answers when no tools are needed\n"
    "- After each tool result, explain in plain English what was done\n"
    "- If no user request is provided, respond with a brief readiness message\n\n"

    "## Auto-Restart Protocol\n"
    "**When:** - When KV >= 75% (as reported in the tool results footer).\n"
    "**Steps:**\n"
    "1. **Save State:** Write current task context to `SESSION.md` using `TOOL:WRITE`.\n"
    "   - Include: Timestamp, KV usage, current task description, pending actions, and last conversation summary.\n"
    "   - Don't check if SESSION.md already exists from another session. just use TOOL:WRITE.\n"
    "2. **Trigger Restart:** Call `TOOL:RESTART` to start over.\n\n"
    "**Example `SESSION.md` Content:**\n"
    "```markdown\n"
    "# Session State Snapshot\n"
    "**Timestamp:** <date> <time>\n"
    "**Current Task:** <task description>\n"
    "**Pending Actions:**\n"
    "- <action 1>\n"
    "- <action 2>\n"
    "**Last Output:**\n"
    "<last few lines of conversation>\n"
    "```\n\n";

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

std::string NitroConfig::introspect() const {
  static constexpr std::string_view tmpl =
    "{{\n"
    "  \"model_path\":     \"{}\",\n"
    "  \"embed_path\":     \"{}\",\n"
    "  \"sandbox\":        \"{}\",\n"
    "  \"n_ctx\":          {},\n"
    "  \"n_batch\":        {},\n"
    "  \"n_gpu_layers\":   {},\n"
    "  \"temperature\":    {},\n"
    "  \"top_p\":          {},\n"
    "  \"min_p\":          {},\n"
    "  \"top_k\":          {},\n"
    "  \"penalty_repeat\": {},\n"
    "  \"penalty_freq\":   {},\n"
    "  \"penalty_present\":{},\n"
    "  \"penalty_last_n\": {},\n"
    "  \"offload_kqv\":    {},\n"
    "  \"kv_cache_preset\":{},\n"
    "  \"rag_top_k\":      {}\n"
    "}}\n";
  return std::format(tmpl,
                     model_path_,
                     embed_path_,
                     sandbox_,
                     n_ctx_,
                     n_batch_,
                     n_gpu_layers_,
                     temperature_,
                     top_p_,
                     min_p_,
                     top_k_,
                     penalty_repeat_,
                     penalty_freq_,
                     penalty_present_,
                     penalty_last_n_,
                     offload_kqv_,
                     kv_preset_to_string(),
                     rag_top_k_);
}

