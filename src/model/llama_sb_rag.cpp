// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <cstddef>
#include <ios>
#include <ostream>

#include "llama_sb.h"
#include "llama_sb_rag.h"
#include "string_utils.h"

namespace fs = std::filesystem;

static constexpr uint32_t MAGIC = 0x52414744;
static constexpr size_t MIN_CHUNK = 40;
static constexpr const char *INSTRUCT_EMBED = "Instruct: Represent this API documentation for code retrieval\nQuery: ";
static constexpr const char *INSTRUCT_QUERY = "Instruct: Given a programming question, retrieve relevant API documentation\nQuery: ";

enum class ChunkType {
  Function, Struct, Enum, Typedef, Defines, Other
};

static std::string type_name(ChunkType t) {
  switch (t) {
  case ChunkType::Function: return "function";
  case ChunkType::Struct:   return "struct";
  case ChunkType::Enum:     return "enum";
  case ChunkType::Typedef:  return "typedef";
  case ChunkType::Defines:  return "defines";
  default:                  return "other";
  }
}

/* ── state machine ─────────────────────────────────────────── */

enum class State {
  Idle, BlockComment, LineComment, Declaration, Struct, Defines
};

template<typename EmitChunk>

static bool chunk_file(const fs::path &path, EmitChunk emit_chunk) {
  std::ifstream f(path);
  if (!f) {
    return false;
  }

  const std::string source = path.filename().string();

  State     state       = State::Idle;
  std::string chunk;
  ChunkType chunk_type  = ChunkType::Other;
  int       brace_depth = 0;
  int       paren_depth = 0;
  int       define_count = 0;

  auto flush = [&](ChunkType t) {
    emit_chunk(source, t, chunk);
    chunk.clear();
    state       = State::Idle;
    brace_depth = 0;
    paren_depth = 0;
  };

  std::string line;
  while (std::getline(f, line)) {
    /* trim trailing CR */
    if (!line.empty() && line.back() == '\r') line.pop_back();

    /* find first non-whitespace for prefix checks */
    size_t trim_pos = 0;
    while (trim_pos < line.size() &&
           (line[trim_pos] == ' ' || line[trim_pos] == '\t')) ++trim_pos;
    const std::string trimmed = line.substr(trim_pos);

    /* ── #define handling ─────────────────────────────────── */
    if (utils::starts_with(trimmed, "#define ")) {
      if (state == State::BlockComment || state == State::LineComment) {
        chunk += line + "\n";
        state = State::Defines;
        define_count = 1;
      } else if (state == State::Defines) {
        chunk += line + "\n";
        define_count++;
      } else {
        if (chunk.size() >= MIN_CHUNK) emit_chunk(source, chunk_type, chunk);
        chunk.clear();
        chunk += line + "\n";
        state = State::Defines;
        define_count = 1;
      }
      continue;
    }

    /* non-define while in define group */
    if (state == State::Defines) {
      flush(ChunkType::Defines);
      define_count = 0;
      /* fall through to process this line normally */
    }

    /* ── block comment start ──────────────────────────────── */
    if ((utils::starts_with(trimmed, "/*") || utils::starts_with(trimmed, "/**")) &&
        state == State::Idle) {
      if (chunk.size() >= MIN_CHUNK) emit_chunk(source, chunk_type, chunk);
      chunk.clear();
      chunk_type = ChunkType::Other;
      chunk += line + "\n";
      state = (trimmed.find("*/", 2) != std::string::npos)
        ? State::LineComment
        : State::BlockComment;
      continue;
    }

    /* ── inside block comment ─────────────────────────────── */
    if (state == State::BlockComment) {
      chunk += line + "\n";
      if (trimmed.find("*/") != std::string::npos)
        state = State::LineComment;
      continue;
    }

    /* ── // line comment ──────────────────────────────────── */
    if (utils::starts_with(trimmed, "//")) {
      if (state == State::Idle) {
        if (chunk.size() >= MIN_CHUNK) emit_chunk(source, chunk_type, chunk);
        chunk.clear();
        chunk += line + "\n";
        state = State::LineComment;
      } else if (state == State::LineComment) {
        chunk += line + "\n";
      }
      continue;
    }

    /* ── blank line ───────────────────────────────────────── */
    if (utils::is_blank(trimmed)) {
      if (state == State::LineComment) {
        flush(ChunkType::Other);
      }
      else if (state == State::Idle && chunk.size() >= MIN_CHUNK) {
        flush(chunk_type);
      }
      continue;
    }

    /* ── skip preprocessor noise ──────────────────────────── */
    if (utils::starts_with(trimmed, "#ifndef") || utils::starts_with(trimmed, "#ifdef")  ||
        utils::starts_with(trimmed, "#endif")  || utils::starts_with(trimmed, "#pragma") ||
        utils::starts_with(trimmed, "#include")) {
      if (state == State::LineComment || state == State::BlockComment) {
        chunk.clear();
        state = State::Idle;
      }
      continue;
    }

    /* ── typedef struct / enum start ─────────────────────── */
    if ((utils::starts_with(trimmed, "typedef struct") ||
         utils::starts_with(trimmed, "typedef enum")   ||
         utils::starts_with(trimmed, "struct ")         ||
         utils::starts_with(trimmed, "enum "))          &&
        (state == State::Idle || state == State::LineComment)) {

      if (state == State::Idle && chunk.size() >= MIN_CHUNK)
        emit_chunk(source, chunk_type, chunk);

      /* preserve any comment already in chunk */
      if (state == State::Idle) chunk.clear();

      chunk += line + "\n";
      chunk_type = utils::starts_with(trimmed, "typedef") ? ChunkType::Typedef
        : utils::starts_with(trimmed, "enum ")   ? ChunkType::Enum
        : ChunkType::Struct;
      state = State::Struct;
      for (char c : line) {
        if (c == '{') ++brace_depth;
        if (c == '}') --brace_depth;
      }
      if (brace_depth <= 0 && line.find(';') != std::string::npos)
        flush(chunk_type);
      continue;
    }

    /* ── inside struct/enum body ──────────────────────────── */
    if (state == State::Struct) {
      chunk += line + "\n";
      for (char c : line) {
        if (c == '{') ++brace_depth;
        if (c == '}') --brace_depth;
      }
      if (brace_depth <= 0 && line.find(';') != std::string::npos)
        flush(chunk_type);
      continue;
    }

    /* ── function / other declaration ────────────────────── */
    if (state == State::LineComment || state == State::Idle) {
      if (state == State::Idle && chunk.size() >= MIN_CHUNK) {
        emit_chunk(source, chunk_type, chunk);
        chunk.clear();
      }
      chunk += line + "\n";
      chunk_type = ChunkType::Function;
      state = State::Declaration;
      for (char c : line) {
        if (c == '(') ++paren_depth;
        if (c == ')') --paren_depth;
      }
      if (paren_depth <= 0 && line.find(';') != std::string::npos)
        flush(ChunkType::Function);
      continue;
    }

    /* ── multi-line declaration ───────────────────────────── */
    if (state == State::Declaration) {
      chunk += line + "\n";
      for (char c : line) {
        if (c == '(') ++paren_depth;
        if (c == ')') --paren_depth;
      }
      if (paren_depth <= 0 && line.find(';') != std::string::npos)
        flush(ChunkType::Function);
      continue;
    }
  }

  /* flush remainder */
  if (chunk.size() >= MIN_CHUNK) emit_chunk(source, chunk_type, chunk);

  return true;
}

/* ── prose chunking ────────────────────────────────────────── */

static constexpr size_t PROSE_TARGET  = 800;
static constexpr size_t PROSE_MAX     = 1200;
static constexpr size_t PROSE_MIN     = 80;

//
// split a long paragraph on sentence boundaries
//
static void split_long_paragraph(const std::string &para,
                                  std::vector<std::string> &out) {
  std::string buf;
  for (size_t i = 0; i < para.size(); i++) {
    buf += para[i];
    if ((para[i] == '.' || para[i] == '!' || para[i] == '?') &&
        buf.size() >= PROSE_TARGET) {
      /* look ahead for whitespace to confirm sentence end */
      size_t j = i + 1;
      while (j < para.size() && para[j] == ' ') j++;
      if (j >= para.size() ||
          (para[j] >= 'A' && para[j] <= 'Z') ||
          para[j] == '"' || para[j] == '\'') {
        out.push_back(buf);
        buf.clear();
        i = j - 1; /* skip the space(s) */
      }
    }
  }
  if (buf.size() >= PROSE_MIN) {
    out.push_back(buf);
  }
}

/*
 * Split prose text into chunks of ~PROSE_TARGET chars.
 * Returns the chunk strings (no source/type metadata).
 * This is the pure logic, testable without embeddings.
 */
std::vector<std::string> prose_chunk_split(const std::string &text) {
  /* split on double-newline */
  std::vector<std::string> paragraphs;
  {
    std::string para;
    size_t i = 0;
    while (i < text.size()) {
      if (text[i] == '\n' && i + 1 < text.size() && text[i + 1] == '\n') {
        if (!para.empty()) {
          paragraphs.push_back(para);
          para.clear();
        }
        i += 2;
      } else {
        para += text[i];
        i++;
      }
    }
    if (!para.empty()) {
      paragraphs.push_back(para);
    }
  }

  std::vector<std::string> chunks;
  std::string buf;
  for (const auto &p : paragraphs) {
    /* trim leading/trailing whitespace from paragraph */
    size_t s = 0, e = p.size();
    while (s < e && (p[s] == ' ' || p[s] == '\t' || p[s] == '\n')) s++;
    while (e > s && (p[e-1] == ' ' || p[e-1] == '\t' || p[e-1] == '\n')) e--;
    std::string trimmed = p.substr(s, e - s);
    if (trimmed.empty()) continue;

    if (trimmed.size() > PROSE_MAX) {
      /* flush current buffer first */
      if (buf.size() >= PROSE_MIN) {
        chunks.push_back(buf);
        buf.clear();
      }
      /* split long paragraph on sentences */
      std::vector<std::string> parts;
      split_long_paragraph(trimmed, parts);
      for (const auto &part : parts) {
        if (buf.size() + part.size() > PROSE_TARGET && buf.size() >= PROSE_MIN) {
          chunks.push_back(buf);
          buf.clear();
        }
        buf += part + " ";
      }
    } else {
      if (buf.size() + trimmed.size() > PROSE_TARGET && buf.size() >= PROSE_MIN) {
        chunks.push_back(buf);
        buf.clear();
      }
      buf += trimmed + " ";
    }
  }

  /* flush remainder */
  if (buf.size() >= PROSE_MIN) {
    chunks.push_back(buf);
  }

  return chunks;
}

template<typename EmitChunk>
static bool chunk_prose(const std::string &text,
                        const std::string &source,
                        EmitChunk emit) {
  auto chunks = prose_chunk_split(text);
  for (const auto &c : chunks) {
    emit(source, "prose", c);
  }
  return true;
}

//
// cosine similarity (vectors already L2-normalized)
//
static float rag_cosine(const std::vector<float> &a,
                        const std::vector<float> &b) {
  float dot = 0.0f;
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) {
    dot += a[i] * b[i];
  }
  return dot;
}

//
// build context string from ranked results
//
static std::string rag_build_context(const RagDB &db,
                                     const std::vector<int> &indices,
                                     const std::vector<float> &scores) {
  std::ostringstream out;
  for (size_t i = 0; i < indices.size(); i++) {
    const RagChunk &c = db.chunks[indices[i]];
    out << "// source: " << c.source
        << " [" << c.type << "]"
        << " (score: " << scores[i] << ")\n"
        << c.text << "\n---\n";
  }
  return out.str();
}

//
// index the file
//
bool Llama::rag_index(RagDB &db, const std::string &filepath) {
  bool embed_fail = false;
  auto emit_chunk = [&](const std::string &source, ChunkType type,
                        const std::string &text) {
    if (text.size() > MIN_CHUNK) {
      RagChunk chunk;
      chunk.text = text;
      chunk.source = source;
      chunk.type = type_name(type);
      if (!embed_text(INSTRUCT_EMBED + text, chunk.embedding, db.embed_dim)) {
        embed_fail = true;
      } else {
        db.chunks.push_back(std::move(chunk));
      }
    }
  };

  return !embed_fail && chunk_file(filepath, emit_chunk);
}

//
// index prose text
//
static constexpr const char *INSTRUCT_EMBED_PROSE =
  "Instruct: Represent this reference text for knowledge retrieval\nQuery: ";

bool Llama::rag_index_prose(RagDB &db, const std::string &text,
                            const std::string &source) {
  bool embed_fail = false;
  auto emit_chunk = [&](const std::string &src, const std::string &type,
                        const std::string &chunk_text) {
    if (chunk_text.size() > PROSE_MIN) {
      RagChunk chunk;
      chunk.text = chunk_text;
      chunk.source = src;
      chunk.type = type;
      if (!embed_text(INSTRUCT_EMBED_PROSE + chunk_text,
                      chunk.embedding, db.embed_dim)) {
        embed_fail = true;
      } else {
        db.chunks.push_back(std::move(chunk));
      }
    }
  };

  return !embed_fail && chunk_prose(text, source, emit_chunk);
}

//
// retrieve with session
//
std::string Llama::rag_retrieve(const RagDB &db,
                                const std::string &query,
                                int top_k,
                                RagSession &session) {
  if (db.empty()) {
    _last_error = "no input";
    return {};
  }

  std::vector<float> qvec;
  std::string text = INSTRUCT_QUERY + query;
  if (!embed_text(text, qvec, db.embed_dim)) {
    _last_error = "failed to embed text";
    return {};
  }

  // score all chunks
  std::vector<int>   order(db.size());
  std::iota(order.begin(), order.end(), 0);
  std::vector<float> scores(db.size());
  for (int i = 0; i < db.size(); i++) {
    scores[i] = rag_cosine(qvec, db.chunks[i].embedding);
  }
  ranges::sort(order, [&](int a, int b){ return scores[a] > scores[b]; });

  // collect top_k unseen, within budget, above threshold
  std::vector<int>   result_idx;
  std::vector<float> result_scores;

  for (int idx : order) {
    if (static_cast<int>(result_idx.size()) >= top_k) break;
    if (session.is_seen(idx))            continue;
    if (scores[idx] < session.score_threshold) break; /* sorted, so stop */
    if (!session.budget_ok(db.chunks[idx].text)) break;

    result_idx.push_back(idx);
    result_scores.push_back(scores[idx]);
    session.mark(idx);
    session.charge(db.chunks[idx].text);
  }

  return rag_build_context(db, result_idx, result_scores);
}

std::string Llama::rag_retrieve_multi(RagLibrary &lib,
                                      const std::string &query,
                                      int top_k,
                                      int ctx_size) {
  if (lib.empty()) {
    _last_error = "no input";
    return {};
  }

  std::ostringstream out;
  int cumulative_tokens = 0;

  for (auto &kv : lib.dbs) {
    const std::string &name = kv.first;
    const RagDB &db = kv.second;
    if (db.empty()) continue;

    RagSession session;
    session.init(db.size(), ctx_size);
    session.tokens_used = cumulative_tokens;

    std::string result = rag_retrieve(db, query, top_k, session);
    if (!result.empty()) {
      out << "// ── " << name << " ──\n"
          << result;
      cumulative_tokens = session.tokens_used;
    }
  }

  return out.str();
}

bool RagDB::save(const std::string &path) const {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    return false;
  }

  auto write32 = [&](uint32_t v) { f.write((char*)&v, 4); };
  auto write16 = [&](uint16_t v) { f.write((char*)&v, 2); };
  auto write8  = [&](uint8_t  v) { f.write((char*)&v, 1); };
  auto writestr = [&](const std::string &s, size_t max_len) {
    size_t len = std::min(s.size(), max_len);
    f.write(s.c_str(), (std::streamsize)len);
  };

  write32(MAGIC);              /* magic "RAGD" */
  write32(2);                       /* version      */
  write32((uint32_t)chunks.size()); /* n_chunks     */
  write32((uint32_t)embed_dim);     /* embed_dim    */

  for (const RagChunk &c : chunks) {
    write32((uint32_t)c.text.size());
    f.write(c.text.c_str(), (std::streamsize)c.text.size());

    uint16_t src_len = (uint16_t)std::min(c.source.size(), (size_t)65535);
    write16(src_len);
    writestr(c.source, src_len);

    uint8_t type_len = (uint8_t)std::min(c.type.size(), (size_t)255);
    write8(type_len);
    writestr(c.type, type_len);

    f.write((char*)c.embedding.data(),
            (std::streamsize)(embed_dim * sizeof(float)));
  }

  return f.good();
}

bool RagDB::load(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return false;
  }

  auto read32 = [&]() -> uint32_t {
    uint32_t v = 0; f.read((char*)&v, 4); return v;
  };
  auto read16 = [&]() -> uint16_t {
    uint16_t v = 0; f.read((char*)&v, 2); return v;
  };
  auto read8 = [&]() -> uint8_t {
    uint8_t v = 0; f.read((char*)&v, 1); return v;
  };
  auto readstr = [&](size_t len) -> std::string {
    std::string s(len, '\0');
    f.read(&s[0], (std::streamsize)len);
    return s;
  };

  uint32_t magic   = read32();
  uint32_t version = read32();
  uint32_t n       = read32();
  uint32_t edim    = read32();

  if (magic != MAGIC) {
    return false;
  }
  if (version != 2) {
    return false;
  }

  embed_dim = (int)edim;
  chunks.resize(n);

  for (uint32_t i = 0; i < n; i++) {
    RagChunk &c = chunks[i];

    uint32_t text_len = read32();
    c.text = readstr(text_len);

    uint16_t src_len = read16();
    c.source = readstr(src_len);

    uint8_t type_len = read8();
    c.type = readstr(type_len);

    c.embedding.resize(edim);
    f.read((char*)c.embedding.data(), (std::streamsize)(edim * sizeof(float)));
  }

  return true;
}

/* ── RagLibrary ────────────────────────────────────────────── */

bool RagLibrary::add(const std::string &name, RagDB db) {
  if (name.empty()) {
    return false;
  }
  dbs[name] = std::move(db);
  return true;
}

bool RagLibrary::remove(const std::string &name) {
  return dbs.erase(name) > 0;
}

RagDB *RagLibrary::get(const std::string &name) {
  auto it = dbs.find(name);
  return (it != dbs.end()) ? &it->second : nullptr;
}

const RagDB *RagLibrary::get(const std::string &name) const {
  auto it = dbs.find(name);
  return (it != dbs.end()) ? &it->second : nullptr;
}

std::vector<std::string> RagLibrary::names() const {
  std::vector<std::string> out;
  out.reserve(dbs.size());
  for (const auto &kv : dbs) {
    out.push_back(kv.first);
  }
  return out;
}

int RagLibrary::total_chunks() const {
  int total = 0;
  for (const auto &kv : dbs) {
    total += kv.second.size();
  }
  return total;
}

int RagLibrary::load_dir(const std::string &dir) {
  int count = 0;
  if (!fs::is_directory(dir)) {
    return 0;
  }
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".rag") continue;

    std::string name = entry.path().stem().string();
    RagDB db;
    if (db.load(entry.path().string())) {
      dbs[name] = std::move(db);
      count++;
    }
  }
  return count;
}

bool RagLibrary::save_dir(const std::string &dir) const {
  if (!fs::is_directory(dir)) {
    if (!fs::create_directories(dir)) {
      return false;
    }
  }
  bool ok = true;
  for (const auto &kv : dbs) {
    fs::path p = fs::path(dir) / (kv.first + ".rag");
    if (!kv.second.save(p.string())) {
      ok = false;
    }
  }
  return ok;
}
