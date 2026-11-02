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

#include "knowledge.h"
#include "shell_util.h"
#include "util/json.h"
#include "llama_sb.h"
#include "string_utils.h"

namespace fs = std::filesystem;

KnowledgeManager::KnowledgeManager() = default;

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
  return !utils::trim(result).empty();
}

//
// Index a .zim file from an arbitrary path
//
bool KnowledgeManager::build_from_path(const std::string &zim_path,
                                       Llama &llama,
                                       const ProgressFn &progress) {
  if (!fs::exists(zim_path)) {
    if (progress) {
      progress("ZIM file not found: " + zim_path);
    }
    return false;
  }

  if (!zimdump_available()) {
    if (progress) {
      progress("ERROR: zimdump not found. Build kiwix-tools: "
               "cd zim-tools && meson setup build && meson compile -C build");
    }
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

  if (progress) {
    progress("extracting " + zim_path + " ...");
  }

  // Run zimdump to extract text articles
  const std::string cmd = std::format(
    "zimdump --text '{}' '{}'", zim_path, extract_dir);
  const std::string result = run_shell_command(cmd);

  if (progress) {
    progress("extraction complete, building index ...");
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
    if (progress) {
      progress("ERROR: no text files extracted from ZIM");
    }
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
  int total_chunks = 0;
  int file_idx = 0;

  for (const auto &tf : text_files) {
    file_idx++;
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

    const std::string source_name = tf.filename().string();
    llama.rag_index_prose(db, content, source_name);
    total_chunks = db.size();

    if (progress && (file_idx % 50 == 0 || file_idx == (int)text_files.size())) {
      progress(std::format("embedded {}/{} files ({} chunks)",
                           file_idx, text_files.size(), total_chunks));
    }
  }

  // Clean up extraction directory
  fs::remove_all(extract_dir);

  if (db.empty()) {
    if (progress) {
      progress("ERROR: no chunks produced");
    }
    return false;
  }

  // Save .rag file
  fs::create_directories(rag_dir_);
  const std::string rag_file = rag_path(id);
  if (!db.save(rag_file)) {
    if (progress) {
      progress("ERROR: failed to save " + rag_file);
    }
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