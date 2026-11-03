// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

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
    tui.append_line(ICON_SYS + "failed to load");
    tui.redraw_all();
    return false;
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

  std::string save_dir = join_path(sandbox, ".hali/rag");
  fs::create_directories(save_dir, ec);
  std::string save_path = join_path(save_dir, name + ".rag");
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

  // Create temp extraction directory
  const std::string extract_dir = "/tmp/hali_zim_" + id;
  fs::remove_all(extract_dir);
  fs::create_directories(extract_dir);

  // Run zimdump with spinner, extract articles to the extraction directory
  const std::string cmd = std::format(
    "zimdump dump --dir='{}' --ns=A '{}'", extract_dir, zim_path);
  const std::string result = run_with_spinner(tui, [cmd]() -> std::string {
    return run_shell_command(cmd);
  }, "extracting " + zim_path);

  // Log full zimdump output (verbose "Wrote ..." lines) to a log file
  {
    const std::string log_path = ".hali/zim_extract.log";
    fs::create_directories(".hali");
    std::ofstream logf(log_path, std::ios::app);
    if (logf) {
      auto now = std::chrono::system_clock::now();
      auto t = std::chrono::system_clock::to_time_t(now);
      std::tm tm_buf {};
      localtime_r(&t, &tm_buf);
      char ts[32];
      std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);
      logf << "[" << ts << "] zimdump " << zim_path << "\n";
      logf << result << "\n";
    }
  }

  // Check if zimdump reported an error
  const bool had_error = result.find("[exit code:") != std::string::npos;

  // Verify files were actually extracted (zimdump can exit non-zero
  // after partially writing files, e.g. exit 255 on a corrupt entry)
  int disk_count = 0;
  if (fs::is_directory(extract_dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(extract_dir)) {
      if (entry.is_regular_file()) {
        disk_count++;
      }
    }
  }

  if (disk_count == 0) {
    fail("zimdump produced no files" +
         (had_error ? ": " + utils::trim(result) : ""));
    fs::remove_all(extract_dir);
    return false;
  }

  if (had_error) {
    // Log a warning but proceed — files were written
    const std::string log_path = ".hali/zim_extract.log";
    std::ofstream logf(log_path, std::ios::app);
    if (logf) {
      logf << "WARNING: zimdump exited with error but "
           << disk_count << " files were extracted, proceeding\n";
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

  // Collect all extracted text files
  std::vector<fs::path> text_files;
  if (fs::is_directory(extract_dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(extract_dir)) {
      if (entry.is_regular_file()) {
        text_files.push_back(entry.path());
      }
    }
  }

  if (text_files.empty()) {
    fail("no text files extracted from ZIM");
    fs::remove_all(extract_dir);
    return false;
  }

  if (progress) {
    progress(std::format("found {} text files, embedding ...",
                         text_files.size()));
  }

  // Build RAG index
  RagDB db;
  db.embed_dim = llama.get_embed_dim();

  // Phase 1: parallel read + chunk
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

  if (chunks.empty()) {
    fail("no chunks produced");
    fs::remove_all(extract_dir);
    return false;
  }

  // Phase 2: sequential embed with progress modal
  tui.show_progress_modal("Embedding", (int)chunks.size());
  int total_chunks = 0;
  bool aborted = false;

  for (int i = 0; i < (int)chunks.size(); i++) {
    if (tui.poll_escape()) {
      aborted = true;
      break;
    }
    llama.rag_index_prose(db, chunks[i].content, chunks[i].source_name);
    total_chunks = db.size();
    tui.update_progress(i + 1, (int)chunks.size(), total_chunks);
  }

  tui.dismiss_progress_modal();

  if (aborted) {
    fail("embedding aborted by user");
    fs::remove_all(extract_dir);
    return false;
  }

  // Clean up extraction directory
  fs::remove_all(extract_dir);

  if (db.empty()) {
    fail("no chunks produced");
    return false;
  }

  // Save .rag file
  fs::create_directories(rag_dir_);
  const std::string rag_file = rag_path(id);
  if (!db.save(rag_file)) {
    fail("failed to save " + rag_file);
    return false;
  }

  // Update index
  IndexedSource src;
  src.id = id;
  src.title = id;
  src.zim_path = zim_path;
  src.rag_path = rag_file;
  src.chunks = total_chunks;

  // Timestamp
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf {};
  localtime_r(&t, &tm_buf);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  src.indexed_at = ts;

  // Replace if already in list, else append
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

  // Save index JSON
  save_index_json();

  if (progress) {
    progress(std::format("built {} ({} chunks) -> {}",
                         id, total_chunks, rag_file));
  }
  return true;
}

//
// Load a .rag file into the session library
//
bool KnowledgeManager::load(const std::string &id) {
  const std::string path = rag_path(id);
  if (!fs::exists(path)) {
    return false;
  }

  RagDB db;
  if (!db.load(path)) {
    return false;
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