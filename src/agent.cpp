// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "llama.h"
#include "config.h"
#include "tui.h"
#include "agent.h"
#include "logging.h"
#include "curl.h"
#include "string_utils.h"
#include "file.h"
#include "graph.h"
#include "webview.h"

//
// handling for strip_code_fences
//
static const std::vector<std::string> CODE_EXTENSIONS = {
  ".py",".c",".cpp",".h",".bas",".java",".html",".js",".ts",
  ".json",".yaml",".toml",".sh",".go",".rs",".jsx",".tsx"
};

static std::string read_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return "ERROR: cannot open [" + path + "]";
  }
  std::ostringstream oss; oss << f.rdbuf();
  return oss.str();
}

static bool make_dir(const std::string &path) {
  try {
    std::filesystem::path p(path);
    if (fs::exists(p)) {
      return true;
    }
    std::error_code ec;
    return fs::create_directories(p, ec);
  }
  catch (const std::filesystem::filesystem_error &e) {
    log_write(LEVEL_DEBUG, "mkdir failed [%s]", e.what());
    return false;
  }
}

static std::string join_path(const std::string &a, const std::string &b) {
  if (b.empty()) return a;
  if (b[0] == '/') return b;
  std::string pa = a;
  if (!pa.empty() && pa.back() == '/') pa.pop_back();
  std::string pb = (b.front() == '/') ? b.substr(1) : b;
  return pa + "/" + pb;
}

static std::string list_dir(const std::string &path) {
  std::ostringstream oss;
  std::error_code ec;
  for (const auto &e : fs::directory_iterator(path, ec)) {
    if (ec) break;
    std::string name = e.path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    oss << (e.is_directory() ? "[" + name + "]" : name) << "\n";
  }
  return oss.str();
}

static bool path_in_sandbox(const std::string &sandbox, const std::string &path) {
  std::error_code ec;
  auto base   = fs::canonical(sandbox, ec);  if (ec) return false;
  auto target = fs::weakly_canonical(path, ec);
  std::string bstr = base.string() + "/";
  std::string tstr = target.string();
  return tstr == base.string() || tstr.compare(0, bstr.size(), bstr) == 0;
}

//
// unwrap() - Remove a matching outer "wrapper" from a string.
//
// Trims leading/trailing whitespace first, then checks (in order):
//
//  1. Same-character pairs   "..."  '...'  |...|  `...`
//  2. Mirror pairs           (...)  [...]  {...}
//  3. HTML-like tags         <tag>...</tag>
//  4. Plain angle brackets   <...>          (fallback if tags don't match)
//
// If none of the above apply, returns the whitespace-trimmed input unchanged.
//
// Examples:
//   unwrap("\"hello\"")        -> "hello"
//   unwrap("  [foo]  ")        -> "foo"
//   unwrap("<b>bold</b>")      -> "bold"
//   unwrap("<file>x</file>")   -> "x"
//   unwrap("<hello>")          -> "hello"
//   unwrap("plain")            -> "plain"
//   unwrap("")                 -> ""
//
static std::string unwrap(const std::string &input) {
  if (input.empty()) {
    return input;
  }

  size_t left = 0;
  size_t right = input.length() - 1;

  while (left <= right && std::isspace(static_cast<unsigned char>(input[left]))) {
    left++;
  }
  while (left <= right && std::isspace(static_cast<unsigned char>(input[right]))) {
    right--;
  }

  if (left > right) {
    return "";
  }

  // Same-character pairs: "", '', ||, ``
  // Note: [], {} are NOT same-char pairs — they belong in mirror pairs only
  if (input[left] == input[right]) {
    if (input[left] == '"'  || input[left] == '\'' ||
        input[left] == '|'  || input[left] == '`') {
      return input.substr(left + 1, right - left - 1);
    }
  }

  // Mirror pairs: (), [], {}, but NOT <> (handled below as possible HTML tags)
  if (input[left] != input[right]) {
    if ((input[left] == '(' && input[right] == ')') ||
        (input[left] == '[' && input[right] == ']') ||
        (input[left] == '{' && input[right] == '}')) {
      return input.substr(left + 1, right - left - 1);
    }
  }

  // HTML-like tags: <tag>content</tag>
  // Also handles plain <...> as a fallback at the end
  if (input[left] == '<' && input[right] == '>') {
    // Find end of opening tag
    size_t openTagEnd = left + 1;
    while (openTagEnd <= right && input[openTagEnd] != '>') openTagEnd++;

    if (openTagEnd < right) {
      std::string openTagName = input.substr(left + 1, openTagEnd - left - 1);

      // Find start of closing tag (search backwards for '<')
      size_t closeTagStart = right;
      while (closeTagStart > openTagEnd && input[closeTagStart] != '<') closeTagStart--;

      if (closeTagStart > openTagEnd && input[closeTagStart + 1] == '/') {
        std::string closeTagName = input.substr(closeTagStart + 2, right - closeTagStart - 2);

        if (!openTagName.empty() && openTagName == closeTagName) {
          // Return content between the tags
          return input.substr(openTagEnd + 1, closeTagStart - openTagEnd - 1);
        }
      }
    }

    // Fallback: plain <...> with no matching HTML tags — unwrap the angle brackets
    return input.substr(left + 1, right - left - 1);
  }

  return input.substr(left, right - left + 1);
}

static std::string strip_code_fences(const std::string &filename,
                                     const std::string &src) {
  auto ext = fs::path(filename).extension().string();
  bool is_code = ranges::any_of(CODE_EXTENSIONS, [&](const std::string &e){ return ext == e; });
  if (!is_code) {
    return unwrap(src);
  }
  auto pos = src.find("```");
  if (pos == std::string::npos) {
    return src;
  }
  auto nl = src.find('\n', pos + 3);
  if (nl == std::string::npos) {
    return src;
  }
  std::string inner = src.substr(nl + 1);
  auto end = inner.rfind("```");
  if (end != std::string::npos) {
    inner = inner.substr(0, end);
  }
  return inner;
}

static bool hasDangerousPatterns(const std::string &command) {
  return ((command.find('|') != std::string::npos) ||
          (command.find('>') != std::string::npos) ||
          (command.find('<') != std::string::npos) ||
          (command.find("rm ") != std::string::npos));
}

static std::string tool_run(const NitroConfig &cfg, Tui &tui, const std::string &arg1, const std::string &arg2) {
  const std::string args = arg1 + " " + arg2;
  if (cfg.permission_prompt_ && !tui.confirm_dialog(std::format("Allow {} {} to run?", arg1, arg2))) {
    return "ERROR: prevented by user";
  } else {
    bool permitted = ranges::any_of(cfg.run_allowed_, [&](const std::string &a) {return a == arg1;});
    if ((!permitted || hasDangerousPatterns(args)) && !tui.confirm_dialog(std::format("Allow {} {} to run?", arg1, arg2))) {
      return "ERROR: '" + arg1 + "' is not in the TOOL:RUN allowlist.";
    }
  }
  const std::string command = args + " 2>&1";
  tui.show_tool("running: " + command);
  FILE *fp = popen(command.c_str(), "r");
  if (!fp) {
    return "ERROR: popen failed";
  }
  std::string out;
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    out += buf;
  }
  pclose(fp);
  if (out.size() > 4096) {
    out = out.substr(0, 4096) + "\n…(truncated)";
  }
  return out;
}

static void broadcast_reload(const NitroConfig &cfg, Tui &tui) {
  if (cfg.web_port_ != -1) {
    tui.show_tool("reload browser");
    webview::broadcast_reload();
  }
}

static void broadcast_message(const NitroConfig &cfg, Tui &tui, const std::string &message) {
  if (cfg.web_port_ != -1 && !utils::is_blank(message)) {
    tui.show_tool("send to browser");
    webview::broadcast_message(message);
  }
}

void Agent::apply_generation_params() const {
  llama_->add_stop("<|turn|>");
  llama_->add_stop("<|im_end|>");
  llama_->set_max_tokens(512000);
  llama_->set_temperature(cfg_.temperature_);
  llama_->set_top_k(cfg_.top_k_);
  llama_->set_top_p(cfg_.top_p_);
  llama_->set_min_p(cfg_.min_p_);
  llama_->set_penalty_repeat(cfg_.penalty_repeat_);
  llama_->set_penalty_freq(cfg_.penalty_freq_);
  llama_->set_penalty_present(cfg_.penalty_present_);
  llama_->set_penalty_last_n(cfg_.penalty_last_n_);
  llama_->set_log_level(cfg_.log_level_);
}

//
// Shows a modal loading popup while the model loads.
//
bool Agent::setup_model() {
  if (cfg_.model_path_.empty()) {
    tui_.append_line(ICON_SYS + "No model loaded.  Use /model <path> to load a GGUF.");
    tui_.redraw_all();
    return false;
  }

  // Show a modal popup so the user knows loading is in progress.
  std::string model_name = fs::path(cfg_.model_path_).filename().string();
  tui_.show_modal_popup("Loading " + model_name);
  // Destroy the iterator first — it holds references into the llama context.
  // Freeing llama while iter is still alive causes use-after-free / load failure.
  iter_.reset();
  model_loaded_ = false;
  llama_ = std::make_unique<Llama>();

  apply_generation_params();

  LlamaLoad load;
  load.model_path = cfg_.model_path_;
  load.n_ctx = cfg_.n_ctx_;
  load.n_batch = cfg_.n_batch_;
  load.n_gpu_layers = cfg_.n_gpu_layers_;
  load.log_level = cfg_.log_level_;
  load.offload_kqv = cfg_.offload_kqv_;
  load.kv_cache_preset = cfg_.kv_preset_;

  if (!llama_->load_model(load)) {
    tui_.dismiss_modal_popup();
    tui_.append_line(ICON_ERR + llama_->last_error());
    tui_.redraw_all();
    return false;
  }

  LlamaMemoryInfo mem = llama_->memory_info();
  tui_.dismiss_modal_popup();
  tui_.setup_model(model_name, mem, cfg_.thinking_);

  model_loaded_ = true;
  return true;
}

bool Agent::setup_embed(const std::string &path) {
  tui_.show_modal_popup("Loading embedding model: " + fs::path(path).filename().string());
  tui_.redraw_all();
  embed_llama_ = std::make_unique<Llama>();
  if (!embed_llama_->load_embedding_model(path)) {
    tui_.dismiss_modal_popup();
    tui_.append_line(ICON_ERR + embed_llama_->last_error());
    tui_.redraw_all();
    embed_llama_.reset();
    return false;
  }
  tui_.dismiss_modal_popup();
  rag_db_      = std::make_unique<RagDB>();
  rag_session_ = std::make_unique<RagSession>();
  tui_.append_line(ICON_SYS + "Embedding model ready.");
  tui_.redraw_all();
  return true;
}

void Agent::reset_conversation(const std::string &sysprompt) {
  system_prompt_ = sysprompt;
  llama_->reset();
  cfg_.load_settings();
  apply_generation_params();
  iter_ = std::make_unique<LlamaIter>();
  if (!llama_->add_message(*iter_, "system", system_prompt_)) {
    tui_.append_line(ICON_ERR + "System prompt injection: " + llama_->last_error());
    tui_.redraw_all();
  } else {
    tui_.update_usage(tokens_per_sec(), llama_->memory_info());
  }
}

double Agent::elapsed_seconds() const {
  if (!iter_) return 0.0;
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double>(now - iter_->_t_start).count();
}

float Agent::tokens_per_sec() const {
  if (!iter_) return 0.0f;
  auto now = std::chrono::high_resolution_clock::now();
  double elapsed = std::chrono::duration<double>(now - iter_->_t_start).count();
  if (elapsed <= 0.0 || iter_->_tokens_generated <= 0) return 0.0f;
  return static_cast<float>(iter_->_tokens_generated / elapsed);
}

std::string Agent::memory_info_status() const {
  float kv_percent = llama_->memory_kv_percent();
  auto message = kv_percent > 75 ? "(Warning: Approaching limit)" : "";
  return std::format("\n[KV-INFO] KV Cache: {}%{} [/KV-INFO]", kv_percent, message);
}

std::string Agent::memory_info_text() const {
  if (!model_loaded_) return "No model loaded.";
  LlamaMemoryInfo m = llama_->memory_info();
  std::ostringstream oss;
  oss << "KV cache  : " << m.kv_used << " / " << m.kv_total
      << "  (" << m.kv_percent << "%)\n";
  if (m.vram_total > 0) {
    oss << "VRAM      : " << (m.vram_used >> 20) << " MB / "
        << (m.vram_total >> 20) << " MB  (" << m.vram_percent << "%)\n";
  }
  oss << "GPU layers: " << m.n_layers_gpu << " / " << m.n_layers_total << "\n";
  oss << "CPU layers: " << m.n_layers_cpu << "\n";
  oss << "Advice    : " << m.advice << "\n";
  return oss.str();
}

std::string Agent::rag_tool(const std::string &agent_query) const {
  std::string result;
  if (embed_llama_ && rag_db_ && rag_session_) {
    result = embed_llama_->rag_retrieve(*rag_db_, agent_query, cfg_.rag_top_k_, *rag_session_);
    if (result.empty()) {
      result = std::string("RAG: no context found: ") + embed_llama_->last_error();
    }
  } else {
    result = "RAG: not enabled";
  }
  return result;
}

bool Agent::rag_load_index(const std::string &path) const {
  if (!embed_llama_ || !rag_db_) {
    tui_.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui_.redraw_all();
    return false;
  }

  if (!rag_db_->load(path)) {
    tui_.append_line(ICON_SYS + "failed to load");
    tui_.redraw_all();
  }

  return true;
}

bool Agent::rag_index(const std::string &path) const {
  if (!embed_llama_ || !rag_db_) {
    tui_.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui_.redraw_all();
    return false;
  }

  auto index_one = [&](const std::string &filepath) {
    tui_.append_line(ICON_SYS + "  indexing: " + filepath);
    tui_.redraw_all();
    if (!embed_llama_->rag_index(*rag_db_, filepath)) {
      tui_.append_line(ICON_ERR + "rag_load: " + embed_llama_->last_error());
      tui_.redraw_all();
    }
  };

  // must be set before indexing
  rag_db_->embed_dim = embed_llama_->get_embed_dim();

  fs::path rp(path);
  std::error_code ec;
  if (fs::is_directory(rp, ec)) {
    for (const auto &entry : fs::recursive_directory_iterator(rp, ec)) {
      if (entry.is_regular_file()) {
        index_one(entry.path().string());
      }
    }
  } else {
    index_one(path);
  }

  std::string save_path = join_path(cfg_.sandbox_, "rag-index.bin");
  tui_.append_line(ICON_SYS + "saving index: " + save_path);
  tui_.redraw_all();
  return rag_db_->save(save_path);
}

std::string Agent::restart() {
  if (fs::exists("SESSION.md")) {
    std::vector<std::string> knowledge_files;
    reset_conversation(cfg_.build_system_prompt());
    tui_.append_line(ICON_ERR + "Session restarted");
    tui_.redraw_all();
    return "continue the pending actions found SESSION.md";
  }
  return "save work context to SESSION.md then try again";
}

//
// Tool dispatch
//
std::string Agent::process_tool(const std::string &cmd) {
  const std::string &sandbox = cfg_.sandbox_;

  std::string op, arg1, arg2;
  auto WS = cmd.find_first_of(" \n");
  if (WS == std::string::npos) {
    op = utils::trim(cmd);
  } else {
    op = utils::trim(cmd.substr(0, WS));
    std::string rest = cmd.substr(WS + 1);
    // clear leading WS from rest
    rest.erase(0, rest.find_first_not_of(" \t"));

    // handle space of newline separator
    // TOOL:WRITE src/render_transport.cpp\n#include "render_transport.h"\n...
    // TOOL:WRITE src/render_transport.cpp #include "render_transport.h"\n ...
    auto NL = rest.find('\n');
    auto SP = rest.find(' ');
    int sep;
    if (NL != std::string::npos && (SP == std::string::npos || NL < SP)) {
      sep = NL;
    } else {
      sep = SP;
    }
    if (sep == std::string::npos) {
      arg1 = rest;
    } else {
      arg1 = rest.substr(0, sep);
      arg2 = rest.substr(sep + 1);
    }
  }

  auto resolve = [&](const std::string &p) -> std::string {
    if (p.empty() || p == ".") {
      return sandbox;
    }
    if (p.substr(0, 2) == "./") {
      return join_path(sandbox, p.substr(2));
    }
    if (p[0] == '/') {
      return p;
    }
    return join_path(sandbox, unwrap(p));
  };

  if (op == "TOOL:DATE") {
    tui_.show_tool(op);
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
    return buf;
  }
  if (op == "TOOL:TIME") {
    tui_.show_tool(op);
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return buf;
  }
  if (op == "TOOL:RND") {
    tui_.show_tool(op);
    return std::to_string(static_cast<double>(rand()) / RAND_MAX);
  }
  if (op == "TOOL:RAG") {
    tui_.show_tool(op);
    return rag_tool(arg1);
  }
  if (op == "TOOL:LIST") {
    std::string dir = resolve(arg1);
    tui_.show_tool("listing: " + dir);
    return list_dir(dir);
  }
  if (op == "TOOL:EXISTS") {
    std::string p = resolve(arg1);
    tui_.show_tool("checking: " + p);
    return fs::exists(p) ? "YES" : "NO";
  }
  if (op == "TOOL:READ") {
    tui_.show_tool("reading: " + arg1);
    std::string p = resolve(arg1);
    return read_file(p);
  }
  if (op == "TOOL:WRITE") {
    tui_.show_tool("writing: " + arg1);
    const auto path = resolve(arg1);
    if (!path_in_sandbox(sandbox, path)) {
      return "ERROR: path outside sandbox";
    }
    const auto data = strip_code_fences(arg1, arg2);
    if (!utils::is_blank(cfg_.backup_path_)) {
      const auto confirm = tool_write_backup(cfg_.backup_path_, path);
      if (!utils::starts_with(confirm, "OK")) {
        tui_.append_token(ICON_ERR + confirm + "\n");
        return confirm;
      }
      tui_.show_tool("backup: " + confirm);
    } else if (cfg_.permission_prompt_ && !tui_.confirm_dialog(std::format("Allow model to write {}?", path))) {
      return "ERROR: action prevented by user";
    } else {
      const auto validate = tool_write_validate(path, data);
      if (!validate.empty() && !tui_.confirm_dialog(std::format("[{}] - Allow model to write {}?", validate, path))) {
        return "ERROR: action prevented by user - " + validate;
      }
    }
    const auto result = tool_write(path, data);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result + "\n");
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  }
  if (op == "TOOL:PATCH") {
    tui_.show_tool("patch: " + arg1);
    const auto result = tool_patch(arg1, arg2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result + "\n");
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  }
  if (op == "TOOL:APPEND") {
    tui_.show_tool("append: " + arg1);
    const auto result = tool_append(arg1, arg2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result + "\n");
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  }
  if (op == "TOOL:MKDIR") {
    std::string p = resolve(arg1);
    tui_.show_tool("mkdir: " + arg1);
    if (!path_in_sandbox(sandbox, p)) {
      return "ERROR: path outside sandbox";
    }
    return make_dir(p) ? "OK: created " + arg1 : "ERROR: mkdir failed for " + arg1;
  }
  if (op == "TOOL:CURL") {
    tui_.show_tool("curl: " + arg1);
    return tool_curl(arg1);
  }
  if (op == "TOOL:INTROSPECT") {
    tui_.show_tool("introspecting: " + arg1);
    return cfg_.introspect();
  }
  if (op == "TOOL:ASK") {
    tui_.set_thinking(false);
    tui_.show_tool("asking: " + arg1 + " " + arg2);
    return tui_.readline();
  }
  if (op == "TOOL:RESTART") {
    tui_.show_tool("restart ...");
    return restart();
  }
  if (op == "TOOL:PERMISSION") {
    tui_.set_thinking(false);
    tui_.show_tool("asking permission: " + arg1 + " " + arg2);
    return tui_.confirm_dialog(arg1 + " " + arg2) ? "YES" : "NO";
  }
  if (op == "TOOL:RUN") {
    return tool_run(cfg_, tui_, arg1, arg2);
  }
  if (op == "TOOL:MCP") {
    tui_.show_tool("mcp: " + arg1);
    return mcp_client_.call_tool(arg1, arg2);
  }
  if (op == "TOOL:GRAPH") {
    const int cols = tui_.get_term_cols() * 0.75;
    auto graphResult = tool_graph(cols, arg1 + arg2);
    if (graphResult.success_) {
      tui_.append_lines(graphResult.data_);
    }
    return graphResult.success_ ? "OK" : graphResult.message_;
  }
  return "ERROR: unknown tool: [" + op + "]";
}

void Agent::invoke_tool(const std::string &buffer, const std::string_view template_str) {
  static constexpr std::string KV_START = "[KV-INFO]";
  static constexpr std::string KV_END = "[/KV-INFO]";
  static const std::string_view END_TOOL = "\nNITRO_END_TOOL";
  static const std::string TOOL_RESULT = "NITRO_TOOL_RESULT: ";

  std::string tool;
  if (const auto pos = buffer.rfind(END_TOOL); pos != std::string::npos) {
    tool = buffer.substr(0, pos);
    const auto endTool = buffer.substr(pos);
    if (endTool.length() > END_TOOL.length()) {
      log_write(LEVEL_DEBUG, "ERROR: trailing delimiter: [%s]", endTool.c_str());
    }
  } else {
    tool = buffer;
  }

  // strip any final [KV-INFO] ... [/KV-INFO] mistakenly added by the agent
  if (const auto kvEnd = tool.rfind(KV_END); kvEnd == (tool.length() - KV_END.length())) {
    if (const auto kvStart = tool.rfind(KV_START); kvStart != std::string::npos) {
      tool = buffer.substr(0, kvStart);
      log_write(LEVEL_DEBUG, "stripped KV_INFO details output by agent");
    }
  }

  log_write(LEVEL_DEBUG, "tool request: [%s]", tool.c_str());
  std::string result = process_tool(tool);
  if (result.empty()) {
    return;
  }
  const std::string content = TOOL_RESULT + std::vformat(template_str, std::make_format_args(result)) + memory_info_status();
  log_write(LEVEL_DEBUG, "tool: [%s] result: [%s]", tool.c_str(), result.c_str());
  tui_.update_usage(tokens_per_sec(), llama_->memory_info());
  if (!llama_->add_message(*iter_, "tool_result", content)) {
    tui_.append_line(ICON_ERR + "tool result inject: " + llama_->last_error());
  }
  if (!iter_->_has_next) {
    tui_.append_line(ICON_ERR + "failed to evoke tool response: " + llama_->last_error());
  }
  if (llama_->is_memory_flush()) {
    tui_.append_line(ICON_ERR + "Warning! - memory has been flushed!");
  }
  tui_.redraw_all();
};

//
// Agent turn
//
bool Agent::run_turn(const std::string &user_message) {
  if (!model_loaded_) {
    tui_.append_line(ICON_ERR + "No model loaded. Use /model <path>");
    tui_.redraw_all();
    return false;
  }

  std::string effective_message = user_message;
  if (embed_llama_ && rag_db_ && rag_session_) {
    std::string context = embed_llama_->rag_retrieve(*rag_db_, user_message, cfg_.rag_top_k_, *rag_session_);
    if (!context.empty()) {
      log_write(LEVEL_DEBUG, "RAG: %s", context.c_str());
      effective_message = "Context:\n" + context + "\n\nUser: " + user_message;
    } else {
      log_write(LEVEL_DEBUG, "RAG: no context found [%s]", embed_llama_->last_error());
    }
  }
  if (!iter_) {
    tui_.append_line(ICON_ERR + "Conversation not initialised (call /clear to reset)");
    tui_.redraw_all();
    return false;
  }
  if (!llama_->add_message(*iter_, "user", effective_message)) {
    tui_.append_line(ICON_ERR + "add_message: " + llama_->last_error());
    tui_.redraw_all();
    return false;
  }
  tui_.append_line("Nitro: ");

  // in_think starts false — models that don't use <think> blocks emit
  // visible text immediately.  The spinner activates only while thinking.
  enum {t_init, t_think, t_thunk} think_mode = (cfg_.thinking_ ? t_init : t_thunk);

  tui_.set_thinking(true);
  std::string buffer;

  auto start_think = [&](const std::string &tag) -> void {
    if (think_mode != t_think) {
      if (const auto pos = buffer.find(tag); pos == 0) {
        think_mode = t_think;
        // display preceding text
        buffer = buffer.substr(0, pos);
      }
    }
  };

  auto end_think = [&](const std::string &tag) -> void {
    if (think_mode == t_thunk && llama_->is_gemma_4()) {
      if (const auto pos = buffer.find(tag); pos == 0) {
        // don't print end-think tags when not in think mode
        buffer = "";
      }
    }
    if (think_mode == t_think) {
      if (const auto pos = buffer.find(tag); pos != std::string::npos) {
        if (pos == 0 && llama_->is_gemma_4() && (tag == "<|channel>thought")) {
          //  dont print repeated start-think tag
          buffer = "";
        } else {
          think_mode = t_thunk;
          // tag is either at the start or end of the line
          if (pos > 0) {
            if (auto thought = buffer.substr(0, pos); !utils::is_blank(thought)) {
              tui_.append_token(ICON_THINK + thought + "\n");
            }
          }
          // add a new line here to handle: <channel|>TOOL:LIST
          buffer = '\n' + buffer.substr(pos + tag.length());
        }
      }
    }
  };

  auto fetch_tool = [&]() -> void {
    while (iter_->_has_next && !tui_.is_escape()) {
      const std::string tok = llama_->next(*iter_);
      buffer += tok;
      tui_.tick_spinner();
      if (auto pos = buffer.find("</think>"); pos != std::string::npos) {
        break;
      }
    }
    // log_write(DEBUG_LEVEL, "fetch_tool: \n%s\n\n", buffer.c_str());
  };

  while (iter_->_has_next && !tui_.is_escape()) {
    if (std::string tok = llama_->next(*iter_); tok == "<") {
      // fetch the complete tag
      std::string tag = tok;
      while (iter_->_has_next && tag.find('>') == std::string::npos) {
        tag += llama_->next(*iter_);
      }
      if (tag == "<|think|>") {
        think_mode = t_think;
        continue;
      } else {
        buffer += tag;
      }
    } else {
      buffer += tok;
    }

    // log_write(DEBUG_LEVEL, "%s", buffer.c_str());

    start_think("<think>");
    start_think("<|think|>");
    start_think("<think|>");
    start_think("<|channel>thought");

    end_think("</think>");
    end_think("</|think|>");
    end_think("</|think>");
    end_think("<think|>");
    end_think("<channel|>");

    if (think_mode == t_think) {
      tui_.set_tokens_per_sec(tokens_per_sec());
      tui_.tick_spinner();
    }
    auto tool_start = buffer.find("TOOL:");
    if (tool_start == 0) {
      fetch_tool();
      invoke_tool(utils::trim(buffer), "TOOL_RESULT: {}");
      buffer.clear();
      think_mode = t_init;
      continue;
    }
    auto pos = buffer.find('\n');
    if (pos != std::string::npos) {
      if (think_mode == t_think) {
        if (auto thought = buffer.substr(0, pos + 1); !utils::is_blank(thought)) {
          tui_.append_token(ICON_THINK + thought);
        }
      } else {
        auto token = buffer.substr(0, pos + 1);
        tui_.append_token(token);
        broadcast_message(cfg_, tui_, token);
      }
      buffer = buffer.substr(pos + 1);
    }
  }

  if (!buffer.empty()) {
    tui_.append_token(buffer + "\n");
    broadcast_message(cfg_, tui_, buffer);
  }

  tui_.set_thinking(false);
  tui_.update_usage(tokens_per_sec(), llama_->memory_info());

  char stat[128];
  const auto pattern = ICON_SYS + "%.1f tok/s  (%d tokens)  %.1fs elapsed  KV %.1f%%";
  std::snprintf(stat, sizeof(stat), pattern.c_str(),
                static_cast<double>(tui_.get_tokens_per_sec()),
                iter_->_tokens_generated,
                elapsed_seconds(),
                static_cast<double>(tui_.get_kv_percent()));
  tui_.append_line(stat);
  tui_.redraw_all();
  return true;
}
