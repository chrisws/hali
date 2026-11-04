// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>

#include "knowledge.h"
#include "util/platform.h"
#include "shell_util.h"
#include "util/json.h"
#include "util/logging.h"
#include "llama_sb.h"
#include "string_utils.h"
#include "agent_utils.h"
#include "ui/tui.h"
#include "ui/icon.h"

namespace fs = std::filesystem;

KnowledgeManager::KnowledgeManager() = default;

//
// Load an embedding model
//
bool KnowledgeManager::setup_embed(const std::string &path, Tui &tui) {
  tui.show_loading_modal_popup("embedding model: " + fs::path(path).filename().string());
  tui.redraw_all();
  embed_llama_ = std::make_unique<Llama>();
  if (!embed_llama_->load_embedding_model(path)) {
    tui.dismiss_modal_popup();
    tui.append_line(ICON_ERR + embed_llama_->last_error());
    tui.redraw_all();
    embed_llama_.reset();
    return false;
  }
  tui.dismiss_modal_popup();
  tui.append_line(ICON_SYS + "Embedding model ready.");
  tui.redraw_all();
  return true;
}

//
// Query the RAG library with caching
//
std::string KnowledgeManager::query(const std::string &text, int top_k, int n_ctx) {
  if (text == last_query_ && !last_result_.empty()) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim", "query: cache hit (%zu chars)",
                last_result_.size());
    }
    return last_result_;
  }
  std::string result;
  if (active()) {
    result = embed_llama_->rag_retrieve_multi(library_, text, top_k, n_ctx);
    if (result.empty()) {
      result = std::string("RAG: no context found: ") + embed_llama_->last_error();
    }
  } else {
    result = "RAG: not enabled";
  }
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "query: top_k=%d n_ctx=%d -> %zu chars",
              top_k, n_ctx, result.size());
  }
  last_query_ = text;
  last_result_ = result;
  return result;
}

//
// Load a pre-built .rag/.bin file into the library
//
bool KnowledgeManager::load_bin(const std::string &path, Tui &tui) {
  if (!active()) {
    tui.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui.redraw_all();
    return false;
  }

  RagDB db;
  if (!db.load(path)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim", "failed to load RAG bin: %s", path.c_str());
    }
    tui.append_line(ICON_SYS + "failed to load");
    tui.redraw_all();
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "loaded RAG bin: %s (%d chunks, dim=%d)",
              path.c_str(), db.size(), db.embed_dim);
  }

  fs::path p(path);
  std::string name = p.stem().string();
  library_.add(name, std::move(db));
  tui.append_line(ICON_SYS + "loaded RAG index: " + name);
  tui.redraw_all();
  return true;
}

//
// Index a source code file or directory
//
bool KnowledgeManager::index_path(const std::string &path, Tui &tui, const std::string &sandbox) {
  if (!active()) {
    tui.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui.redraw_all();
    return false;
  }

  RagDB db;
  db.embed_dim = embed_llama_->get_embed_dim();

  auto index_one = [&](const std::string &filepath) {
    tui.append_line(ICON_SYS + "  indexing: " + filepath);
    tui.redraw_all();
    if (!embed_llama_->rag_index(db, filepath)) {
      tui.append_line(ICON_ERR + "rag_load: " + embed_llama_->last_error());
      tui.redraw_all();
    }
  };

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

  std::string name = rp.stem().string();
  library_.add(name, db);

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "index_path: %s -> %d chunks",
              path.c_str(), db.size());
  }

  std::string save_dir = utils::join_path(sandbox, ".hali/rag");
  fs::create_directories(save_dir, ec);
  std::string save_path = utils::join_path(save_dir, name + ".rag");
  tui.append_line(ICON_SYS + "saving index: " + save_path);
  tui.redraw_all();
  return db.save(save_path);
}

//
// Load all .rag files from a directory
//
int KnowledgeManager::load_dir(const std::string &dir) {
  if (!active()) {
    return 0;
  }
  return library_.load_dir(dir);
}

//
// Full status report for RAGINFO
//
std::string KnowledgeManager::inspect(int top_k) const {
  std::ostringstream oss;
  oss << "RAG Subsystem Status\n";
  oss << "────────────────────\n";

  if (!active()) {
    oss << "State: INACTIVE (no embedding model loaded)\n";
  } else {
    oss << "State: ACTIVE\n";
    oss << "Embed model: loaded\n";
    oss << "Top-K:     " << top_k << "\n";
  }
  oss << "────────────────────\n";
  oss << "Library: " << library_.size() << " DB(s), "
      << library_.total_chunks() << " total chunks\n";

  auto names = library_.names();
  if (names.empty()) {
    oss << "  (empty — no indexes loaded)\n";
  } else {
    for (const auto &name : names) {
      const RagDB *db = library_.get(name);
      if (db) {
        oss << "  • " << name << ": " << db->size()
            << " chunks, dim=" << db->embed_dim << "\n";
      }
    }
  }

  oss << "────────────────────\n";
  oss << "Cache: last_query=\"" << (last_query_.empty() ? "(none)" : last_query_) << "\"\n";
  oss << "       last_result=" << (last_result_.empty() ? "(empty)" : (last_result_.size() > 80 ? std::string(last_result_.begin(), last_result_.begin() + 80) + "…" : last_result_)) << "\n";

  return oss.str();
}

//
// Load the index JSON from index_json_path_
//
bool KnowledgeManager::load_index_json() {
  indexed_.clear();

  if (!fs::exists(index_json_path_)) {
    return true;
  }

  std::ifstream f(index_json_path_);
  if (!f) {
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  f.close();

  auto doc = json::parse(content);
  if (!doc.is_valid()) {
    return false;
  }

  auto root = doc.get_root();
  auto srcs = root.get_child("sources");
  if (!srcs.is_arr()) {
    return true;
  }

  std::vector<json::JsonValue> src_arr;
  srcs.get_array(src_arr);

  for (const auto &r : src_arr) {
    IndexedSource src;
    r.get_str("id", src.id);
    r.get_str("title", src.title);
    r.get_str("zim_path", src.zim_path);
    r.get_str("rag_path", src.rag_path);
    r.get_int("chunks", src.chunks);
    r.get_str("indexed_at", src.indexed_at);
    if (!src.id.empty()) {
      indexed_.push_back(std::move(src));
    }
  }

  return true;
}

//
// Save the index JSON to index_json_path_
//
bool KnowledgeManager::save_index_json() const {
  // Build JSON manually to avoid mutable doc complexity
  std::ostringstream out;
  out << "{\n  \"version\": \"1.0\",\n  \"sources\": [";

  for (size_t i = 0; i < indexed_.size(); i++) {
    const auto &s = indexed_[i];
    if (i > 0) out << ",";
    out << "\n    {\n";
    out << "      \"id\": \"" << s.id << "\",\n";
    out << "      \"title\": \"" << s.title << "\",\n";
    out << "      \"zim_path\": \"" << s.zim_path << "\",\n";
    out << "      \"rag_path\": \"" << s.rag_path << "\",\n";
    out << "      \"chunks\": " << s.chunks << ",\n";
    out << "      \"indexed_at\": \"" << s.indexed_at << "\"\n";
    out << "    }";
  }

  if (!indexed_.empty()) {
    out << "\n  ";
  }
  out << "]\n}\n";

  fs::create_directories(
    fs::path(index_json_path_).parent_path());

  std::ofstream f(index_json_path_);
  if (!f) {
    return false;
  }
  f << out.str();
  return true;
}

const IndexedSource *KnowledgeManager::find(const std::string &id) const {
  for (const auto &s : indexed_) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

std::string KnowledgeManager::rag_path(const std::string &id) const {
  return rag_dir_ + "/" + id + ".rag";
}

bool KnowledgeManager::rag_exists(const std::string &id) const {
  return fs::exists(rag_path(id));
}

//
// Check if zimdump is available
//
bool KnowledgeManager::zimdump_available() {
  const std::string result = run_shell_command("which zimdump 2>/dev/null");
  // run_shell_command appends "[exit code: N]" on failure,
  // so a non-empty result does NOT mean the binary was found.
  if (result.find("[exit code:") != std::string::npos) {
    return false;
  }
  return !utils::trim(result).empty();
}

//
// Return a platform-specific install hint for zimdump
//
std::string KnowledgeManager::zimdump_install_hint() {
  switch (current_platform()) {
    case Platform::Linux:
      return "Install: sudo apt install zim-tools (Debian/Ubuntu)\n"
             "  or build: cd zim-tools && meson setup build && meson compile -C build";
    case Platform::macOS:
      return "Install: brew install zim-tools\n"
             "  or build: cd zim-tools && meson setup build && meson compile -C build";
    case Platform::Windows:
      return "Build from source: cd zim-tools && meson setup build && meson compile -C build\n"
             "  (requires Meson + Ninja, see https://mesonbuild.com/)";
    default:
      return "Build from source: cd zim-tools && meson setup build && meson compile -C build";
  }
}

int KnowledgeManager::zim_extract(const std::string &zim_path,
                                   const std::string &id,
                                   Tui &tui,
                                   const ProgressFn &progress,
                                   std::string &extract_dir) {
  extract_dir = "/tmp/hali_zim_" + id;
  fs::remove_all(extract_dir);
  fs::create_directories(extract_dir);

  const std::string cmd = std::format(
    "zimdump dump --dir='{}' --ns=A '{}'", extract_dir, zim_path);
  const std::string result = run_with_spinner(tui, [cmd]() -> std::string {
    return run_shell_command(cmd);
  }, "extracting " + zim_path);

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "zimdump cmd: %s", cmd.c_str());
    log_write(LEVEL_DEBUG, "zim", "zimdump output: %s", result.c_str());
  }

  const bool had_error = result.find("[exit code:") != std::string::npos;

  int disk_count = 0;
  if (fs::is_directory(extract_dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(extract_dir)) {
      if (entry.is_regular_file()) {
        disk_count++;
      }
    }
  }

  if (disk_count == 0) {
    if (progress) {
      progress("zimdump produced no files" +
               (had_error ? ": " + utils::trim(result) : ""));
    }
    fs::remove_all(extract_dir);
    extract_dir.clear();
    return 0;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "extracted %d files from %s",
              disk_count, zim_path.c_str());
  }

  if (had_error) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim",
                "zimdump exited with error but %d files extracted, proceeding",
                disk_count);
    }
    if (progress) {
      progress(std::format("warning: zimdump exited with error, "
                           "but {} files extracted — continuing",
                           disk_count));
    }
  }

  if (progress) {
    progress(std::format("extracted {} files, building index ...", disk_count));
  }
  return disk_count;
}

//
// Attempt to resolve a path case-insensitively.
// If the exact path exists, return it. Otherwise, scan the parent
// directory for a filename that matches ignoring case.
//
static fs::path resolve_case_insensitive(const fs::path &path) {
  if (fs::exists(path)) {
    return path;
  }
  fs::path parent = path.parent_path();
  std::string filename = path.filename().string();
  std::string filename_lower;
  filename_lower.reserve(filename.size());
  for (char c : filename) {
    filename_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  std::error_code ec;
  if (fs::is_directory(parent, ec)) {
    for (const auto &entry : fs::directory_iterator(parent, ec)) {
      std::string entry_name = entry.path().filename().string();
      std::string entry_lower;
      entry_lower.reserve(entry_name.size());
      for (char c : entry_name) {
        entry_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (entry_lower == filename_lower) {
        return entry.path();
      }
    }
  }
  return path; // original — will fail to open, caller handles it
}

std::vector<std::pair<std::string, std::string>>
KnowledgeManager::zim_read_chunks(
    const std::vector<std::filesystem::path> &text_files,
    const std::string &extract_dir,
    const ProgressFn &progress) {
  struct ChunkData {
    std::string content;
    std::string source_name;
  };
  std::vector<ChunkData> chunks;
  {
    std::mutex chunk_mutex;
    std::atomic<int> read_idx{0};
    const int nthreads =
        std::max(1u, std::thread::hardware_concurrency() - 2);

    if (progress) {
      progress(std::format("reading {} files with {} threads ...",
                           text_files.size(), nthreads));
    }

    auto worker = [&]() {
      // Helper: resolve a redirect stub to its target content.
      // Returns true if we found and read the target.
      auto resolve_redirect = [&](const std::string &stub,
                                  const std::filesystem::path &base_dir,
                                  std::string &out) -> bool {
        // Look for: content="0;url=SOMETHING"
        const std::string marker = "url=";
        auto pos = stub.find(marker);
        if (pos == std::string::npos) {
          return false;
        }
        pos += marker.size();
        auto end = stub.find('"', pos);
        if (end == std::string::npos) {
          return false;
        }
        std::string target = stub.substr(pos, end - pos);
        // URL-decode the target (handle %XX sequences)
        std::string decoded;
        decoded.reserve(target.size());
        for (size_t i = 0; i < target.size(); i++) {
          if (target[i] == '%' && i + 2 < target.size()) {
            char hex[3] = {target[i+1], target[i+2], 0};
            decoded += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
          } else {
            decoded += target[i];
          }
        }
        // Try to read the target file (follow up to 3 chained redirects)
        std::filesystem::path target_path =
          resolve_case_insensitive(
            fs::path(utils::join_path(base_dir.string(), decoded)));
        for (int depth = 0; depth < 3; depth++) {
          std::ifstream tf(target_path);
          if (!tf) {
            return false;
          }
          std::string tcontent((std::istreambuf_iterator<char>(tf)),
                                std::istreambuf_iterator<char>());
          tf.close();
          if (tcontent.find("http-equiv") == std::string::npos) {
            out = std::move(tcontent);
            return true;
          }
          // Chained redirect: resolve relative to current file's parent
          auto tpos = tcontent.find("url=");
          if (tpos == std::string::npos) {
            return false;
          }
          tpos += 4;
          auto tend = tcontent.find('"', tpos);
          if (tend == std::string::npos) {
            return false;
          }
          std::string next = tcontent.substr(tpos, tend - tpos);
          std::string next_decoded;
          next_decoded.reserve(next.size());
          for (size_t j = 0; j < next.size(); j++) {
            if (next[j] == '%' && j + 2 < next.size()) {
              char hex[3] = {next[j+1], next[j+2], 0};
              next_decoded += static_cast<char>(std::strtol(hex, nullptr, 16));
              j += 2;
            } else {
              next_decoded += next[j];
            }
          }
          target_path = resolve_case_insensitive(
            fs::path(utils::join_path(target_path.parent_path().string(),
                               next_decoded)));
        }
        return false;
      };

      const std::filesystem::path base_dir(extract_dir);

      while (true) {
        int idx = read_idx.fetch_add(1);
        if (idx >= (int)text_files.size()) {
          break;
        }
        const auto &tf = text_files[idx];
        std::ifstream f(tf);
        if (!f) {
          continue;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        f.close();
        // Detect redirect stubs: small files with http-equiv refresh
        if (content.size() < 500 &&
            content.find("http-equiv") != std::string::npos) {
          std::string resolved;
          if (resolve_redirect(content, tf.parent_path(), resolved)) {
            content = std::move(resolved);
          } else {
            continue; // unresolvable redirect, skip
          }
        }
        if (content.size() < 80) {
          continue;
        }
        content = utils::strip_html(content);
        if (content.size() < 80) {
          continue;
        }
        std::lock_guard<std::mutex> lock(chunk_mutex);
        chunks.push_back({std::move(content), tf.filename().string()});
      }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < nthreads; i++) {
      threads.emplace_back(worker);
    }
    for (auto &t : threads) {
      t.join();
    }
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "read_chunks: %zu files in, %zu chunks out",
              text_files.size(), chunks.size());
  }

  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(chunks.size());
  for (auto &c : chunks) {
    result.emplace_back(std::move(c.content), std::move(c.source_name));
  }
  return result;
}

int KnowledgeManager::zim_embed_chunks(
    const std::vector<std::pair<std::string, std::string>> &chunks,
    Llama &llama, Tui &tui, RagDB &db) {
  tui.show_progress_modal("Embedding", (int)chunks.size());
  int total_chunks = 0;
  bool aborted = false;

  for (int i = 0; i < (int)chunks.size(); i++) {
    if (tui.poll_escape()) {
      aborted = true;
      break;
    }
    llama.rag_index_prose(db, chunks[i].first, chunks[i].second);
    total_chunks = db.size();
    tui.update_progress(i + 1, (int)chunks.size(), total_chunks);
  }

  tui.dismiss_progress_modal();

  if (aborted) {
    return -1;
  }
  return total_chunks;
}

bool KnowledgeManager::zim_save_and_index(const std::string &id,
                                           const std::string &zim_path,
                                           RagDB &db,
                                           int total_chunks,
                                           const ProgressFn &progress) {
  fs::create_directories(rag_dir_);
  const std::string rag_file = rag_path(id);
  if (!db.save(rag_file)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim", "failed to save %s", rag_file.c_str());
    }
    if (progress) {
      progress("failed to save " + rag_file);
    }
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "saved %s (%d chunks)",
              rag_file.c_str(), total_chunks);
  }

  IndexedSource src;
  src.id = id;
  src.title = id;
  src.zim_path = zim_path;
  src.rag_path = rag_file;
  src.chunks = total_chunks;

  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf {};
  localtime_r(&t, &tm_buf);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  src.indexed_at = ts;

  bool found = false;
  for (auto &existing : indexed_) {
    if (existing.id == id) {
      existing = src;
      found = true;
      break;
    }
  }
  if (!found) {
    indexed_.push_back(std::move(src));
  }

  save_index_json();

  if (progress) {
    progress(std::format("built {} ({} chunks) -> {}",
                         id, total_chunks, rag_file));
  }
  return true;
}

//
// --- ZIM build helpers (file-local) ---
//

//
// Decode a percent-encoded URL string.
//
static std::string url_decode(const std::string &encoded) {
  std::string decoded;
  decoded.reserve(encoded.size());
  for (size_t i = 0; i < encoded.size(); i++) {
    if (encoded[i] == '%' && i + 2 < encoded.size()) {
      char hex[3] = {encoded[i+1], encoded[i+2], 0};
      decoded += static_cast<char>(std::strtol(hex, nullptr, 16));
      i += 2;
    } else {
      decoded += encoded[i];
    }
  }
  return decoded;
}

//
// Check whether a page is a ZIM redirect stub (meta-refresh HTML shell).
//
static bool is_redirect_stub(const std::string &content) {
  return content.size() < 500 &&
         content.find("http-equiv=\"refresh\"") != std::string::npos;
}

//
// Resolve a ZIM redirect stub to its target content.
// Follows up to 3 chained redirects.
// Returns true and sets `out` on success.
//
static bool resolve_redirect(const std::string &stub,
                             const fs::path &base_dir,
                             std::string &out) {
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "resolve_redirect: %s", stub.c_str());
  }
  const std::string marker = "url=";
  auto pos = stub.find(marker);
  if (pos == std::string::npos) {
    return false;
  }
  pos += marker.size();
  auto end = stub.find('"', pos);
  if (end == std::string::npos) {
    return false;
  }
  std::string target = url_decode(stub.substr(pos, end - pos));

  fs::path target_path = resolve_case_insensitive(
    fs::path(utils::join_path(base_dir.string(), target)));
  for (int depth = 0; depth < 3; depth++) {
    std::ifstream tf(target_path);
    if (!tf) {
      return false;
    }
    std::string tcontent((std::istreambuf_iterator<char>(tf)),
                          std::istreambuf_iterator<char>());
    tf.close();
    if (tcontent.find("http-equiv") == std::string::npos) {
      out = std::move(tcontent);
      return true;
    }
    // Chained redirect — resolve relative to the current file's parent
    auto tpos = tcontent.find("url=");
    if (tpos == std::string::npos) {
      return false;
    }
    tpos += 4;
    auto tend = tcontent.find('"', tpos);
    if (tend == std::string::npos) {
      return false;
    }
    target_path = resolve_case_insensitive(
      fs::path(utils::join_path(target_path.parent_path().string(),
                         url_decode(tcontent.substr(tpos, tend - tpos)))));
  }
  return false;
}

//
// Collect all regular files under a directory (recursive).
//
static std::vector<fs::path> collect_text_files(const std::string &dir) {
  std::vector<fs::path> files;
  if (fs::is_directory(dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file()) {
        files.push_back(entry.path());
      }
    }
  }
  return files;
}

//
// Statistics from the ZIM embedding pass.
//
struct ZimEmbedStats {
  int total_chunks = 0;
  int skip_unreadable = 0;
  int skip_short_raw = 0;
  int skip_redirect = 0;
  int skip_short_stripped = 0;
  int skip_no_chunks = 0;
  int resolved_redirects = 0;
  bool aborted = false;
};

//
// Stream text files through the embedding model, populating `db`.
// Returns stats. Caller is responsible for showing/dismissing
// the progress modal.
//
static ZimEmbedStats embed_text_files(const std::vector<fs::path> &text_files,
                                      const fs::path &base_dir,
                                      Llama &llama,
                                      Tui &tui,
                                      RagDB &db) {
  ZimEmbedStats stats;
  db.embed_dim = llama.get_embed_dim();

  for (size_t i = 0; i < text_files.size(); i++) {
    if (tui.poll_escape()) {
      stats.aborted = true;
      break;
    }

    std::ifstream f(text_files[i]);
    if (!f) {
      stats.skip_unreadable++;
      continue;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    if (content.size() < 80) {
      stats.skip_short_raw++;
      continue;
    }

    if (is_redirect_stub(content)) {
      std::string resolved;
      if (resolve_redirect(content, text_files[i].parent_path(), resolved)) {
        content = std::move(resolved);
        stats.resolved_redirects++;
      } else {
        stats.skip_redirect++;
        continue;
      }
    }

    content = utils::strip_html(content);
    if (content.size() < 80) {
      stats.skip_short_stripped++;
      continue;
    }

    int before = db.size();
    llama.rag_index_prose(db, content,
                          text_files[i].filename().string());
    if (db.size() == before) {
      stats.skip_no_chunks++;
    }
    stats.total_chunks = db.size();
    tui.update_progress((int)(i + 1), (int)text_files.size(),
                        stats.total_chunks);
  }

  return stats;
}

//
// Index a .zim file from an arbitrary path
//
bool KnowledgeManager::build_from_path(const std::string &zim_path,
                                       Llama &llama,
                                       Tui &tui,
                                       const ProgressFn &progress,
                                       std::string *error) {
  auto fail = [progress, error](const std::string &msg) {
    if (progress) {
      progress(msg);
    }
    if (error) {
      *error = msg;
    }
  };

  if (!fs::exists(zim_path)) {
    fail("ZIM file not found: " + zim_path);
    return false;
  }

  if (!zimdump_available()) {
    fail("zimdump not found. " + zimdump_install_hint());
    return false;
  }

  // Derive id from filename stem
  fs::path zp(zim_path);
  const std::string id = zp.stem().string();

  // Check if already indexed
  if (rag_exists(id)) {
    if (progress) {
      progress("already indexed: " + id);
    }
    return true;
  }

  // Phase 1: Extract
  std::string extract_dir;
  int disk_count = zim_extract(zim_path, id, tui, progress, extract_dir);
  if (disk_count == 0) {
    fail("zimdump produced no files");
    return false;
  }

  // Collect all extracted text files
  auto text_files = collect_text_files(extract_dir);
  if (text_files.empty()) {
    fail("no text files extracted from ZIM");
    fs::remove_all(extract_dir);
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "build_from_path: %zu text files found in %s",
              text_files.size(), extract_dir.c_str());
  }

  if (progress) {
    progress(std::format("found {} text files, embedding ...",
                         text_files.size()));
  }

  // Phase 2: Embed
  RagDB db;
  tui.show_progress_modal("Embedding", (int)text_files.size());
  auto stats = embed_text_files(text_files, fs::path(extract_dir),
                                llama, tui, db);
  tui.dismiss_progress_modal();

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim",
              "build_from_path stats: %zu files | resolved: %d redirects | "
              "skipped: %d unreadable, %d short-raw, %d redirect, "
              "%d short-stripped, %d no-chunks | %d chunks produced",
              text_files.size(), stats.resolved_redirects,
              stats.skip_unreadable, stats.skip_short_raw,
              stats.skip_redirect, stats.skip_short_stripped,
              stats.skip_no_chunks, stats.total_chunks);
  }

  if (progress) {
    progress(std::format(
      "index stats: {} files | {} redirects resolved | "
      "skipped: {} unreadable, {} short-raw, {} redirect, "
      "{} short-stripped, {} no-chunks | {} chunks produced",
      text_files.size(), stats.resolved_redirects,
      stats.skip_unreadable, stats.skip_short_raw,
      stats.skip_redirect, stats.skip_short_stripped,
      stats.skip_no_chunks, stats.total_chunks));
  }

  // Clean up extraction directory
  fs::remove_all(extract_dir);

  if (stats.aborted) {
    fail("embedding aborted by user");
    return false;
  }

  if (db.empty()) {
    fail("no chunks produced");
    return false;
  }

  // Phase 3: Save + index
  return zim_save_and_index(id, zim_path, db, stats.total_chunks, progress);
}

//
// Load a .rag file into the session library
//
bool KnowledgeManager::load(const std::string &id) {
  const std::string path = rag_path(id);
  if (!fs::exists(path)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim", "load: %s not found", path.c_str());
    }
    return false;
  }

  RagDB db;
  if (!db.load(path)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim", "load: failed to load %s", path.c_str());
    }
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "load: %s (%d chunks)",
              id.c_str(), db.size());
  }

  return library_.add(id, std::move(db));
}

//
// Unload from session library
//
bool KnowledgeManager::unload(const std::string &id) {
  return library_.remove(id);
}

//
// Remove .rag file and entry from index
//
bool KnowledgeManager::remove(const std::string &id) {
  bool ok = true;
  const std::string rag = rag_path(id);

  if (fs::exists(rag)) {
    ok = fs::remove(rag) && ok;
  }

  // Remove from indexed list
  indexed_.erase(
    std::remove_if(indexed_.begin(), indexed_.end(),
                   [&id](const IndexedSource &s) { return s.id == id; }),
    indexed_.end());

  save_index_json();

  // Also unload from library if loaded
  library_.remove(id);

  return ok;
}

//
// Load all .rag files from rag_dir_
//
int KnowledgeManager::load_all() {
  return library_.load_dir(rag_dir_);
}

//
// Format loaded sources for display
//
std::string KnowledgeManager::list_loaded() const {
  if (library_.empty()) {
    return "No sources loaded.";
  }

  std::ostringstream out;
  auto names = library_.names();
  for (const auto &name : names) {
    const RagDB *db = library_.get(name);
    out << "  " << name << "  ("
        << (db ? db->size() : 0) << " chunks)\n";
  }
  return out.str();
}

//
// Format indexed sources for display
//
std::string KnowledgeManager::list_indexed() const {
  if (indexed_.empty()) {
    return "No sources indexed.";
  }

  std::ostringstream out;
  for (const auto &s : indexed_) {
    out << "  " << s.id << "  ("
        << s.chunks << " chunks)  "
        << s.zim_path << "\n";
  }
  return out.str();
}

//
// Get display labels for all indexed sources (for picker)
//
std::vector<std::string> KnowledgeManager::indexed_labels() const {
  std::vector<std::string> labels;
  labels.reserve(indexed_.size());
  for (const auto &s : indexed_) {
    labels.push_back(s.id + " \u2014 " +
                     std::to_string(s.chunks) + " chunks");
  }
  return labels;
}

//
// Get ids for all indexed sources (parallel to indexed_labels)
//
std::vector<std::string> KnowledgeManager::indexed_ids() const {
  std::vector<std::string> ids;
  ids.reserve(indexed_.size());
  for (const auto &s : indexed_) {
    ids.push_back(s.id);
  }
  return ids;
}

//
// Get display labels for currently loaded sources (for picker)
//
std::vector<std::string> KnowledgeManager::loaded_labels() const {
  std::vector<std::string> labels;
  auto names = library_.names();
  labels.reserve(names.size());
  for (const auto &name : names) {
    const RagDB *db = library_.get(name);
    labels.push_back(name + " (" + std::to_string(db ? db->size() : 0) + " chunks)");
  }
  return labels;
}

//
// Get ids for currently loaded sources (parallel to loaded_labels)
//
std::vector<std::string> KnowledgeManager::loaded_ids() const {
  std::vector<std::string> ids;
  auto names = library_.names();
  ids.reserve(names.size());
  for (const auto &name : names) {
    ids.push_back(name);
  }
  return ids;
}

//
// Status summary
//
std::string KnowledgeManager::status() const {
  std::ostringstream out;
  out << "Indexed sources:       " << indexed_.size() << "\n";
  out << "Loaded in session:     " << library_.size() << "\n";
  out << "Total chunks:          " << library_.total_chunks() << "\n";

  // Disk usage for rag dir
  if (fs::is_directory(rag_dir_)) {
    size_t total = 0;
    int count = 0;
    for (const auto &entry : fs::directory_iterator(rag_dir_)) {
      if (entry.is_regular_file() && entry.path().extension() == ".rag") {
        total += entry.file_size();
        count++;
      }
    }
    out << "RAG files:             " << count
        << " (" << (total / 1024) << " KB)\n";
  }

  out << "zimdump available:     "
      << (zimdump_available() ? "yes" : "no") << "\n";

  return out.str();
}
