// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef KNOWLEDGE_H
#define KNOWLEDGE_H

#include <functional>
#include <string>
#include <vector>
#include "llama_sb_rag.h"

struct Llama;

//
// A single entry from the kiwix meta-index
//
struct KnowledgeSource {
  std::string id;
  std::string version;
  std::string title;
  std::string description;
  std::string url;
  int         size_mb = 0;
  std::string category;
};

//
// Progress callback: called with status messages during long operations
//
using ProgressFn = std::function<void(const std::string &)>;

//
// KnowledgeManager
//
// Owns the RagLibrary and the ZIM download/build pipeline.
// Parses the bundled meta-index, downloads ZIM files, extracts text,
// builds RAG indexes, and manages loading/unloading of .rag files.
//
struct KnowledgeManager {
  explicit KnowledgeManager();
  ~KnowledgeManager() = default;

  //
  // Parse the meta-index JSON file. Returns true on success.
  //
  bool load_meta(const std::string &path);

  //
  // Find a source by its unique id. Returns nullptr if not found.
  //
  const KnowledgeSource *find(const std::string &id) const;

  //
  // Download a ZIM file from the source's URL to zim_dir_/<id>.zim
  //
  bool download(const std::string &id, const ProgressFn &progress);

  //
  // Extract text from a downloaded ZIM and build a RAG index.
  // Requires the ZIM file to already exist in zim_dir_.
  // Saves the result to rag_dir_/<id>.rag
  //
  bool build(const std::string &id, Llama &llama, const ProgressFn &progress);

  //
  // Load a built .rag file into the session library
  //
  bool load(const std::string &id);

  //
  // Remove a source from the session library
  //
  bool unload(const std::string &id);

  //
  // Delete .rag and .zim files for a source
  //
  bool remove(const std::string &id);

  //
  // Load all .rag files from rag_dir_ into the library
  // Returns the count loaded
  //
  int load_all();

  //
  // Format available sources as a display string
  //
  std::string list_sources() const;

  //
  // Format loaded sources as a display string
  //
  std::string list_loaded() const;

  //
  // Get display labels for all known sources (for picker)
  // Returns "id — title" strings
  //
  std::vector<std::string> source_labels() const;

  //
  // Get display labels for currently loaded sources (for picker)
  //
  std::vector<std::string> loaded_labels() const;

  //
  // Get ids for all known sources (parallel to source_labels)
  //
  std::vector<std::string> source_ids() const;

  //
  // Get ids for currently loaded sources (parallel to loaded_labels)
  //
  std::vector<std::string> loaded_ids() const;

  //
  // Status summary: disk usage, loaded count, source count
  //
  std::string status() const;

  //
  // Access the underlying library
  //
  RagLibrary &library() { return library_; }
  const RagLibrary &library() const { return library_; }

  //
  // Check if zimdump is available on the system
  //
  static bool zimdump_available();

  //
  // Check if a ZIM file exists for the given id
  //
  bool zim_exists(const std::string &id) const;

  //
  // Check if a .rag file exists for the given id
  //
  bool rag_exists(const std::string &id) const;

  //
  // Paths
  //
  std::string zim_path(const std::string &id) const;
  std::string rag_path(const std::string &id) const;

  //
  // Set directories (call before use)
  //
  void set_rag_dir(const std::string &dir)  { rag_dir_ = dir; }
  void set_zim_dir(const std::string &dir)  { zim_dir_ = dir; }

private:
  std::vector<KnowledgeSource> sources_;
  RagLibrary                   library_;
  std::string                  rag_dir_ = ".hali/rag";
  std::string                  zim_dir_ = ".hali/zim";
};

#endif
