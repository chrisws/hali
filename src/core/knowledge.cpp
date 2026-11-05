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
  int loaded = load_all();
  if (loaded > 0) {
    tui.append_line(ICON_SYS + std::to_string(loaded) + " index(es) loaded from disk.");
  }
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
  library_.add(name, std::move(db));

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
        // Show build stats if available
        const IndexedSource *src = find(name);
        if (src && src->stats.total_files > 0) {
          const auto &s = src->stats;
          int skipped = s.skip_unreadable + s.skip_short_raw +
                        s.skip_redirect + s.skip_short_stripped +
                        s.skip_no_chunks + s.skip_filtered;
          oss << "      files: " << s.total_files
              << " | redirects resolved: " << s.resolved_redirects
              << " | skipped: " << skipped;
          if (s.skip_short_raw > 0)
            oss << " (" << s.skip_short_raw << " short)";
          if (s.skip_redirect > 0)
            oss << " (" << s.skip_redirect << " redirect)";
          if (s.skip_short_stripped > 0)
            oss << " (" << s.skip_short_stripped << " stripped)";
          if (s.skip_no_chunks > 0)
            oss << " (" << s.skip_no_chunks << " no-chunks)";
          if (s.skip_unreadable > 0)
            oss << " (" << s.skip_unreadable << " unreadable)";
          if (s.skip_filtered > 0)
            oss << " (" << s.skip_filtered << " filtered)";
          oss << "\n";
        }
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
    auto st = r.get_child("stats");
    if (st.is_object()) {
      st.get_int("total_files",         src.stats.total_files);
      st.get_int("resolved_redirects",  src.stats.resolved_redirects);
      st.get_int("skip_unreadable",     src.stats.skip_unreadable);
      st.get_int("skip_short_raw",      src.stats.skip_short_raw);
      st.get_int("skip_redirect",       src.stats.skip_redirect);
      st.get_int("skip_short_stripped", src.stats.skip_short_stripped);
      st.get_int("skip_no_chunks",      src.stats.skip_no_chunks);
      st.get_int("skip_filtered",       src.stats.skip_filtered);
    }
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
    out << "      \"indexed_at\": \"" << s.indexed_at << "\",\n";
    out << "      \"stats\": {\n";
    out << "        \"total_files\": "         << s.stats.total_files         << ",\n";
    out << "        \"resolved_redirects\": "  << s.stats.resolved_redirects  << ",\n";
    out << "        \"skip_unreadable\": "     << s.stats.skip_unreadable     << ",\n";
    out << "        \"skip_short_raw\": "      << s.stats.skip_short_raw      << ",\n";
    out << "        \"skip_redirect\": "       << s.stats.skip_redirect       << ",\n";
    out << "        \"skip_short_stripped\": " << s.stats.skip_short_stripped << ",\n";
    out << "        \"skip_no_chunks\": "      << s.stats.skip_no_chunks      << ",\n";
    out << "        \"skip_filtered\": "       << s.stats.skip_filtered       << "\n";
    out << "      }\n";
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