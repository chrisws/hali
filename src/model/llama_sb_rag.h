// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef LLAMA_SB_RAG_H
#define LLAMA_SB_RAG_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

struct RagChunk {
  std::string        text;
  std::string        source;
  std::string        type;
  std::vector<float> embedding;
  /* mmap mode: points into the memory-mapped file, avoiding a
   * heap copy of the (potentially large) embedding vector. */
  const float*       embedding_ptr = nullptr;

  /* Unified accessor: returns the embedding from either the
   * heap vector (normal load) or the mmap pointer (mmap load).
   * This lets callers use one code path for both modes. */
  const float* get_embedding() const {
    return embedding_ptr ? embedding_ptr : embedding.data();
  }
};

/* ── on-disk chunk (variable-length text) ──────────────────── */
/*
 * db header  (16 bytes):
 *   uint32  magic      = 0x52414744  "RAGD"
 *   uint32  version    = 2
 *   uint32  n_chunks
 *   uint32  embed_dim
 *
 * per chunk:
 *   uint32  text_len
 *   char[]  text          (text_len bytes, no null)
 *   uint16  source_len
 *   char[]  source        (source_len bytes, no null)
 *   uint8   type_len
 *   char[]  type          (type_len bytes, no null)
 *   float[] embedding     (embed_dim floats)
 */
struct RagDB {
  std::vector<RagChunk> chunks;
  int embed_dim = 0;

  // mmap backend (set when loaded via load_mmap)
  void*        mmap_ptr    = nullptr;
  size_t       mmap_size   = 0;
  std::string  mmap_path;

  ~RagDB() {
    if (mmap_ptr) {
      #if defined(_WIN32)
      UnmapViewOfFile(mmap_ptr);
      #else
      munmap(mmap_ptr, mmap_size);
      #endif
      mmap_ptr = nullptr;
    }
  }

  /* Non-copyable: the mmap pointer must not be shared between
   * two objects (double-munmap would crash). Movable so it can
   * live in std::map<std::string, RagDB> via operator[] + move-assign. */
  RagDB() = default;
  RagDB(const RagDB &) = delete;
  RagDB &operator=(const RagDB &) = delete;
  RagDB(RagDB &&other) noexcept
    : chunks(std::move(other.chunks)), embed_dim(other.embed_dim),
      mmap_ptr(other.mmap_ptr), mmap_size(other.mmap_size),
      mmap_path(std::move(other.mmap_path)) {
    other.mmap_ptr = nullptr;
    other.mmap_size = 0;
  }
  RagDB &operator=(RagDB &&other) noexcept {
    if (this != &other) {
      if (mmap_ptr) {
        #if defined(_WIN32)
        UnmapViewOfFile(mmap_ptr);
        #else
        munmap(mmap_ptr, mmap_size);
        #endif
      }
      chunks = std::move(other.chunks);
      embed_dim = other.embed_dim;
      mmap_ptr = other.mmap_ptr;
      mmap_size = other.mmap_size;
      mmap_path = std::move(other.mmap_path);
      other.mmap_ptr = nullptr;
      other.mmap_size = 0;
    }
    return *this;
  }

  bool is_mmap() const { return mmap_ptr != nullptr; }

  bool load(const std::string &path);
  bool load_mmap(const std::string &path);
  bool save(const std::string &path) const;

  //
  // Incremental file I/O for batched indexing.
  // init_file writes the 16-byte header with n_chunks=0.
  // append_chunks writes current chunks to the file and clears them.
  // finalize_file patches n_chunks in the header to the actual count.
  //
  bool init_file(const std::string &path) const;
  bool append_chunks(const std::string &path);
  static bool finalize_file(const std::string &path, uint32_t n_chunks);

  int  size()  const { return (int)chunks.size(); }
  bool empty() const { return chunks.empty(); }
};

//
// split prose text into chunks of ~800 chars (paragraph-aware)
// returns the chunk strings, no metadata
//
std::vector<std::string> prose_chunk_split(const std::string &text);

//
// per-session deduplication + token budget
//
struct RagSession {
  std::vector<bool> seen;        /* sized to db.size() on init  */
  int  tokens_used  = 0;
  int  tokens_max   = 0;         /* set to your n_ctx           */
  float score_threshold = 0.60f; /* skip weak matches           */

  void init(int n_chunks, int ctx_size) {
    seen.assign(n_chunks, false);
    tokens_used = 0;
    tokens_max  = ctx_size;
  }

  void reset() {
    std::fill(seen.begin(), seen.end(), false);
    tokens_used = 0;
  }

  bool is_seen(int idx)  const { return idx >= 0 && idx < (int)seen.size() && seen[idx]; }
  void mark(int idx)           { if (idx >= 0 && idx < (int)seen.size()) seen[idx] = true; }

  /* rough token estimate: 1 token ≈ 4 chars */
  bool budget_ok(const std::string &text) const {
    return tokens_max == 0 ||
           (tokens_used + (int)text.size() / 4) < (int)(tokens_max * 0.85f);
  }

  void charge(const std::string &text) {
    tokens_used += (int)text.size() / 4;
  }
};

/*
 * Multi-DB container: holds named RagDB instances so the
 * agent can search across multiple knowledge sources.
 */
struct RagLibrary {
  std::map<std::string, RagDB> dbs;
  /* Memory cap: reject DBs that would push total chunks past this
   * limit. Prevents unbounded RAM growth when loading many .rag
   * files. 0 = unlimited (default). */
  int max_total_chunks = 0;

  /* Takes rvalue ref because RagDB is non-copyable (mmap pointer). */
  bool add(const std::string &name, RagDB &&db);
  bool remove(const std::string &name);
  RagDB *get(const std::string &name);
  const RagDB *get(const std::string &name) const;
  std::vector<std::string> names() const;
  int  size()         const { return (int)dbs.size(); }
  bool empty()        const { return dbs.empty(); }
  int  total_chunks() const;

  /* load all .rag files from a directory; returns count loaded */
  int  load_dir(const std::string &dir);
  /* save all DBs to a directory; returns true on success */
  bool save_dir(const std::string &dir) const;
};

#endif