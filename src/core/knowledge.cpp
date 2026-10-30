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

#include "knowledge.h"
#include "shell_util.h"
#include "util/json.h"
#include "llama_sb.h"
#include "string_utils.h"

namespace fs = std::filesystem;

KnowledgeManager::KnowledgeManager() = default;

//
// Parse the meta-index JSON
//
bool KnowledgeManager::load_meta(const std::string &path) {
  std::ifstream f(path);
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
    return false;
  }

  std::vector<json::JsonValue> src_arr;
  srcs.get_array(src_arr);

  for (const auto &r : src_arr) {
    KnowledgeSource src;
    r.get_str("id", src.id);
    r.get_str("version", src.version);
    r.get_str("title", src.title);
    r.get_str("description", src.description);
    r.get_str("url", src.url);
    r.get_str("category", src.category);
    r.get_int("size_mb", src.size_mb);
    if (!src.id.empty()) {
      sources_.push_back(std::move(src));
    }
  }

  return !sources_.empty();
}

const KnowledgeSource *KnowledgeManager::find(const std::string &id) const {
  for (const auto &s : sources_) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

std::string KnowledgeManager::zim_path(const std::string &id) const {
  return zim_dir_ + "/" + id + ".zim";
}

std::string KnowledgeManager::rag_path(const std::string &id) const {
  return rag_dir_ + "/" + id + ".rag";
}

bool KnowledgeManager::zim_exists(const std::string &id) const {
  return fs::exists(zim_path(id));
}

bool KnowledgeManager::rag_exists(const std::string &id) const {
  return fs::exists(rag_path(id));
}

//
// Download a ZIM file via curl
//
bool KnowledgeManager::download(const std::string &id,
                                const ProgressFn &progress) {
  const KnowledgeSource *src = find(id);
  if (!src) {
    return false;
  }

  // Ensure zim directory exists
  fs::create_directories(zim_dir_);

  const std::string dest = zim_path(id);
  if (fs::exists(dest)) {
    if (progress) {
      progress("already downloaded: " + dest);
    }
    return true;
  }

  if (progress) {
    progress(std::format("downloading {} MB: {}", src->size_mb, src->url));
  }

  // Use curl with -L (follow redirects), -o (output), -f (fail on error)
  const std::string cmd = std::format(
    "curl -L -f -o '{}' '{}'", dest, src->url);
  const std::string result = run_shell_command(cmd);

  if (!fs::exists(dest) || fs::file_size(dest) == 0) {
    if (progress) {
      progress("download failed: " + result);
    }
    return false;
  }

  if (progress) {
    progress(std::format("downloaded {} ({} bytes)",
                         dest, fs::file_size(dest)));
  }
  return true;
}

//
// Check if zimdump is available
//
bool KnowledgeManager::zimdump_available() {
  const std::string result = run_shell_command("which zimdump 2>/dev/null");
  return !utils::trim(result).empty();
}

//
// Extract text from ZIM and build RAG index
//
bool KnowledgeManager::build(const std::string &id, Llama &llama,
                             const ProgressFn &progress) {
  const KnowledgeSource *src = find(id);
  if (!src) {
    return false;
  }

  const std::string zim_file = zim_path(id);
  if (!fs::exists(zim_file)) {
    if (progress) {
      progress("ZIM file not found: " + zim_file +
               ". Run /index download " + id + " first.");
    }
    return false;
  }

  if (!zimdump_available()) {
    if (progress) {
      progress("ERROR: zimdump not found. Install kiwix-tools: "
               "sudo apt install kiwix-tools");
    }
    return false;
  }

  // Create temp extraction directory
  const std::string extract_dir = "/tmp/hali_zim_" + id;
  fs::remove_all(extract_dir);
  fs::create_directories(extract_dir);

  if (progress) {
    progress("extracting " + zim_file + " ...");
  }

  // Run zimdump to extract text articles
  const std::string cmd = std::format(
    "zimdump --text '{}' '{}'", zim_file, extract_dir);
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
// Remove .rag and .zim files
//
bool KnowledgeManager::remove(const std::string &id) {
  bool ok = true;
  const std::string rag = rag_path(id);
  const std::string zim = zim_path(id);

  if (fs::exists(rag)) {
    ok = fs::remove(rag) && ok;
  }
  if (fs::exists(zim)) {
    ok = fs::remove(zim) && ok;
  }

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
// Format available sources for display
//
std::string KnowledgeManager::list_sources() const {
  if (sources_.empty()) {
    return "No sources in meta-index.";
  }

  std::ostringstream out;
  std::string current_cat;
  for (const auto &s : sources_) {
    if (s.category != current_cat) {
      current_cat = s.category;
      out << "\n" << current_cat << ":\n";
    }
    const std::string status = rag_exists(s.id) ? " [indexed]" :
                               zim_exists(s.id) ? " [downloaded]" : "";
    out << "  " << s.id << "  "
        << s.title << "  ("
        << s.size_mb << " MB)" << status << "\n";
  }
  return out.str();
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
// Get display labels for all known sources (for picker)
//
std::vector<std::string> KnowledgeManager::source_labels() const {
  std::vector<std::string> labels;
  labels.reserve(sources_.size());
  for (const auto &s : sources_) {
    std::string label = s.id + " \u2014 " + s.title;
    if (rag_exists(s.id)) {
      label += " [indexed]";
    } else if (zim_exists(s.id)) {
      label += " [downloaded]";
    }
    labels.push_back(label);
  }
  return labels;
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
// Get ids for all known sources (parallel to source_labels)
//
std::vector<std::string> KnowledgeManager::source_ids() const {
  std::vector<std::string> ids;
  ids.reserve(sources_.size());
  for (const auto &s : sources_) {
    ids.push_back(s.id);
  }
  return ids;
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
  out << "Sources in meta-index: " << sources_.size() << "\n";
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

  // Disk usage for zim dir
  if (fs::is_directory(zim_dir_)) {
    size_t total = 0;
    int count = 0;
    for (const auto &entry : fs::directory_iterator(zim_dir_)) {
      if (entry.is_regular_file() && entry.path().extension() == ".zim") {
        total += entry.file_size();
        count++;
      }
    }
    out << "ZIM files:             " << count
        << " (" << (total / (1024 * 1024)) << " MB)\n";
  }

  out << "zimdump available:     "
      << (zimdump_available() ? "yes" : "no") << "\n";

  return out.str();
}
