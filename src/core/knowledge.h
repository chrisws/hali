// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef KNOWLEDGE_H
#define KNOWLEDGE_H

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "llama_sb_rag.h"

struct Llama;
struct Tui;

//
// Build-time statistics for a ZIM index
//
struct BuildStats {
  int total_files         = 0;
  int resolved_redirects  = 0;
  int skip_unreadable     = 0;
  int skip_short_raw      = 0;
  int skip_redirect       = 0;
  int skip_short_stripped = 0;
  int skip_no_chunks      = 0;
  int skip_duplicate      = 0;
  int skip_filtered       = 0;
};

//
// A single entry in the .hali/kiwix-index.json tracking file
//
struct IndexedSource {
  std::string  id;
  std::string  title;
  std::string  zim_path;
  std::string  rag_path;
  int          chunks = 0;
  std::string  indexed_at;
  BuildStats   stats;
};

//
// Progress callback: called with status messages during long operations
//
using ProgressFn = std::function<void(const std::string &)>;

//
// Shared state for the ZIM build pipeline
//
struct BuildContext {
  std::string extract_dir;
  std::string rag_file;
  std::string id;
  std::string zim_path;
  bool        append = false;
  uint32_t    existing_chunks = 0;
  int         total_chunks = 0;
  int         skip_unreadable = 0;
  int         skip_short_raw = 0;
  int         skip_redirect = 0;
  int         skip_short_stripped = 0;
  int         skip_no_chunks = 0;
  int         skip_duplicate = 0;
  int         resolved_redirects = 0;
  int         skip_filtered = 0;
  bool        aborted = false;
};

//
// KnowledgeManager
//
// Owns the RagLibrary and the ZIM indexing pipeline.
// Indexes .zim files from arbitrary paths, builds RAG indexes,
// maintains a .hali/kiwix-index.json tracking file, and manages
// loading/unloading of .rag files.
//
struct KnowledgeManager {
  explicit KnowledgeManager();
  ~KnowledgeManager() = default;

  //
  // --- Embedding model management ---
  //

  //
  // Load an embedding model. Returns true on success.
  //
  bool setup_embed(const std::string &path, Tui &tui);

  //
  // Access the embedding model (nullptr if not loaded)
  //
  Llama *embed_model() const { return embed_llama_.get(); }

  //
  // Check if the RAG subsystem is active (embed model loaded)
  //
  bool active() const { return embed_llama_ != nullptr; }

  //
  // --- Query / retrieval ---
  //

  //
  // Query the RAG library. Returns context string.
  // Caches last query/result.
  //
  std::string query(const std::string &text, int top_k, int n_ctx);

  //
  // Clear the query cache
  //
  void reset_cache() const { last_query_.clear(); last_result_.clear(); }

  //
  // --- Index loading / building ---
  //

  //
  // Load a pre-built .rag/.bin file into the library by path.
  //
  bool load_bin(const std::string &path, Tui &tui);

  //
  // Index a source code file or directory, save to .hali/rag/
  //
  bool index_path(const std::string &path, Tui &tui, const std::string &sandbox);

  //
  // Load all .rag files from a directory into the library
  //
  int load_dir(const std::string &dir);

  //
  // --- Inspection ---
  //

  //
  // Full status report for RAGINFO
  //
  std::string inspect(int top_k) const;

  //
  // --- Knowledge index management (Kiwix .zim) ---
  //

  //
  // Load the index JSON from index_json_path_. Returns true on success.
  //
  bool load_index_json();

  //
  // Save the index JSON to index_json_path_.
  //
  bool save_index_json() const;

  //
  // Find an indexed source by id. Returns nullptr if not found.
  //
  const IndexedSource *find(const std::string &id) const;

  //
  // Index a .zim file from an arbitrary path.
  // Extracts text with zimdump, embeds chunks, saves .rag file,
  // and updates the index JSON.
  // If error is non-null, the specific failure reason is written to it.
  //
  bool build_from_path(const std::string &zim_path,
                       Llama &llama,
                       Tui &tui,
                       const ProgressFn &progress,
                       std::string *error = nullptr,
                       const std::string &filter_path = "",
                       bool append = false);

  //
  // (private) save rag file reference and update index JSON with stats
  //
  bool zim_save_and_index(const std::string &id, const std::string &zim_path,
                          int total_chunks, const BuildStats &stats,
                          const ProgressFn &progress);

  //
  // Load a built .rag file into the session library
  //
  bool load(const std::string &id);

  //
  // Remove a source from the session library
  //
  bool unload(const std::string &id);

  //
  // Delete .rag file for a source and remove from index JSON
  //
  bool remove(const std::string &id);

  //
  // Load all .rag files from rag_dir_ into the library
  // Returns the count loaded
  //
  int load_all();

  //
  // Format loaded sources as a display string
  //
  std::string list_loaded() const;

  //
  // Format indexed sources as a display string
  //
  std::string list_indexed() const;

  //
  // Get display labels for all indexed sources (for picker)
  // Returns "id — title (N chunks)" strings
  //
  std::vector<std::string> indexed_labels() const;

  //
  // Get ids for all indexed sources (parallel to indexed_labels)
  //
  std::vector<std::string> indexed_ids() const;

  //
  // Get display labels for currently loaded sources (for picker)
  //
  std::vector<std::string> loaded_labels() const;

  //
  // Get ids for currently loaded sources (parallel to loaded_labels)
  //
  std::vector<std::string> loaded_ids() const;

  //
  // Status summary: disk usage, loaded count, indexed count
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
  // Return a platform-specific install hint for zimdump
  //
  static std::string zimdump_install_hint();

  //
  // Check if a .rag file exists for the given id
  //
  bool rag_exists(const std::string &id) const;

  //
  // Paths
  //
  std::string rag_path(const std::string &id) const;

  //
  // Set directories (call before use)
  //
  void set_rag_dir(const std::string &dir)       { rag_dir_ = dir; }
  void set_index_json_path(const std::string &p) { index_json_path_ = p; }

private:
  int zim_extract(const std::string &zim_path, const std::string &id,
                  const ProgressFn &progress,
                  std::string &extract_dir);

  bool batch_embed(const std::vector<std::filesystem::path> &files,
                   Llama &llama,
                   Tui &tui,
                   const ProgressFn &progress,
                   BuildContext &ctx,
                   std::string *error) const;

  std::vector<IndexedSource> indexed_;
  RagLibrary                 library_;
  std::string                rag_dir_ = ".hali/rag";
  std::string                index_json_path_ = ".hali/kiwix-index.json";
  std::unique_ptr<Llama>     embed_llama_;
  mutable std::string        last_query_;
  mutable std::string        last_result_;
};

#endif