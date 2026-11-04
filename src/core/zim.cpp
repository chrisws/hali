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
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "knowledge.h"
#include "util/platform.h"
#include "shell_util.h"
#include "util/logging.h"
#include "llama_sb.h"
#include "string_utils.h"
#include "ui/tui.h"
#include "agent_utils.h"

namespace fs = std::filesystem;

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
                                  const ProgressFn &progress,
                                  std::string &extract_dir) {
  extract_dir = ".hali/tmp/hali_zim_" + id;
  if (fs::exists(extract_dir)) {
    fs::remove_all(extract_dir);
  }
  fs::create_directories(extract_dir);

  const std::string cmd = std::format("zimdump dump --dir='{}' --ns=A '{}'", extract_dir, zim_path);
  const std::string result = run_shell_command(cmd);

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
// Thread-safe cache for case-insensitive directory lookups.
// Each directory is scanned at most once; subsequent lookups are O(1).
//
struct CaseInsensitiveCache {
  std::mutex mutex;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, fs::path>> dirs;

  void clear() {
    std::lock_guard<std::mutex> lock(mutex);
    dirs.clear();
  }

  static std::string to_lower(const std::string &s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
      r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
  }

  fs::path lookup(const fs::path &path) {
    if (fs::is_regular_file(path)) {
      return path;
    }
    fs::path parent = path.parent_path();
    std::string key = parent.string();
    std::string filename_lower = to_lower(path.filename().string());

    std::lock_guard<std::mutex> lock(mutex);
    auto &dir_map = dirs[key];
    if (dir_map.empty()) {
      std::error_code ec;
      if (fs::is_directory(parent, ec)) {
        for (const auto &entry : fs::directory_iterator(parent, ec)) {
          if (entry.is_regular_file()) {
            dir_map[to_lower(entry.path().filename().string())] =
              entry.path();
          }
        }
      }
    }
    auto it = dir_map.find(filename_lower);
    if (it != dir_map.end()) {
      return it->second;
    }
    return path; // not found — caller handles failure
  }
};

static CaseInsensitiveCache g_case_cache;

//
// Resolve a path case-insensitively using a cached directory scan.
// Returns the original path if no match is found.
//
static fs::path resolve_case_insensitive(const fs::path &path) {
  return g_case_cache.lookup(path);
}

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
  return content.size() < 2000 &&
    content.find("http-equiv=\"refresh\"") != std::string::npos;
}

//
// Resolve a ZIM redirect stub to its target content.
// Follows up to 3 chained redirects.
// Returns true and sets `out` and `resolved_path` on success.
//
static bool resolve_redirect(const std::string &stub,
                             const fs::path &base_dir,
                             std::string &out,
                             fs::path &resolved_path) {
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

  fs::path target_path = resolve_case_insensitive(fs::path(utils::join_path(base_dir.string(), target)));
  for (int depth = 0; depth < 3; depth++) {
    if (!std::filesystem::is_regular_file(target_path)) {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write(LEVEL_DEBUG, "zim", "redirect target is a directory, skipping: %s", target_path.c_str());
      }
      break;
    }
    std::ifstream tf(target_path);
    if (!tf) {
      return false;
    }
    std::string tcontent((std::istreambuf_iterator<char>(tf)),
                         std::istreambuf_iterator<char>());
    tf.close();
    if (tcontent.find("http-equiv") == std::string::npos) {
      out = std::move(tcontent);
      resolved_path = target_path;
      return true;
    }
    // Chained redirect — resolve relative to the extraction root
    auto tpos = tcontent.find("url=");
    if (tpos == std::string::npos) {
      return false;
    }
    tpos += 4;
    auto tend = tcontent.find('"', tpos);
    if (tend == std::string::npos) {
      return false;
    }
    target_path = resolve_case_insensitive(fs::path(utils::join_path(base_dir.string(),
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

bool KnowledgeManager::zim_save_and_index(const std::string &id,
                                          const std::string &zim_path,
                                          int total_chunks,
                                          const BuildStats &stats,
                                          const ProgressFn &progress) {
  const std::string rag_file = rag_path(id);
  if (!fs::exists(rag_file)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "zim", "rag file not found: %s", rag_file.c_str());
    }
    if (progress) {
      progress("rag file not found: " + rag_file);
    }
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "indexed %s (%d chunks)",
              rag_file.c_str(), total_chunks);
  }

  IndexedSource src;
  src.id = id;
  src.title = id;
  src.zim_path = zim_path;
  src.rag_path = rag_file;
  src.chunks = total_chunks;
  src.stats = stats;

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
// Index a .zim file from an arbitrary path.
// Uses a batch pipeline with incremental disk writes to bound memory:
// only one batch of chunks is held in RAM at a time.
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

  fs::path zp(zim_path);
  const std::string id = zp.stem().string();

  if (rag_exists(id)) {
    if (progress) {
      progress("already indexed: " + id);
    }
    return true;
  }

  // Phase 1: Extract
  std::string extract_dir;
  int disk_count = 0;
  run_with_spinner(tui, [&]() -> std::string {
    disk_count = zim_extract(zim_path, id, progress, extract_dir);
    return "";
  }, "extracting " + zim_path);
  if (disk_count == 0) {
    fail("zimdump produced no files");
    return false;
  }

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
    progress(std::format("found {} text files, embedding in batches ...",
                         text_files.size()));
  }

  // Clear case-insensitive cache from any previous build
  g_case_cache.clear();

  // Phase 2: Batch embed with incremental disk writes
  constexpr int BATCH_SIZE = 50;
  const std::string rag_file = rag_path(id);
  fs::create_directories(rag_dir_);

  RagDB batch_db;
  batch_db.embed_dim = llama.get_embed_dim();
  if (!batch_db.init_file(rag_file)) {
    fail("failed to init " + rag_file);
    fs::remove_all(extract_dir);
    return false;
  }

  int total_chunks = 0;
  int skip_unreadable = 0;
  int skip_short_raw = 0;
  int skip_redirect = 0;
  int skip_short_stripped = 0;
  int skip_no_chunks = 0;
  int skip_duplicate = 0;
  int resolved_redirects = 0;
  bool aborted = false;

  // Dedup: track canonical content by 64-bit hash of its path.
  // Multiple redirect stubs can point to the same article;
  // we only want to embed each article once.
  std::unordered_set<uint64_t> seen;
  auto path_hash = [](const fs::path &p) -> uint64_t {
    return static_cast<uint64_t>(std::hash<std::string>{}(p.string()));
  };

  tui.show_progress_modal("Embedding", (int)text_files.size());

  for (size_t i = 0; i < text_files.size(); i += BATCH_SIZE) {
    const size_t end = std::min(i + (size_t)BATCH_SIZE, text_files.size());

    for (size_t j = i; j < end; j++) {
      if (tui.poll_escape()) {
        aborted = true;
        break;
      }

      std::ifstream f(text_files[j]);
      if (!f) {
        skip_unreadable++;
        continue;
      }
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      f.close();

      if (content.size() < 80) {
        skip_short_raw++;
        continue;
      }

      // Determine canonical identity for dedup
      fs::path canonical = text_files[j];
      if (is_redirect_stub(content)) {
        std::string resolved;
        fs::path resolved_path;
        if (resolve_redirect(content, extract_dir, resolved, resolved_path)) {
          content = std::move(resolved);
          canonical = resolved_path;
          resolved_redirects++;
        } else {
          skip_redirect++;
          continue;
        }
      }

      // Skip if we've already embedded this article
      uint64_t h = path_hash(canonical);
      if (!seen.insert(h).second) {
        skip_duplicate++;
        continue;
      }

      content = utils::strip_html(content);
      if (content.size() < 80) {
        skip_short_stripped++;
        continue;
      }

      int before = batch_db.size();
      llama.rag_index_prose(batch_db, content,
                            text_files[j].filename().string());
      if (batch_db.size() == before) {
        skip_no_chunks++;
      }
    }

    // Append this batch's chunks to disk, then free memory
    if (!batch_db.chunks.empty()) {
      const int batch_count = batch_db.size();
      if (!batch_db.append_chunks(rag_file)) {
        tui.dismiss_progress_modal();
        fail("failed to append chunks to " + rag_file);
        fs::remove_all(extract_dir);
        fs::remove(rag_file);
        return false;
      }
      total_chunks += batch_count;
    }

    tui.update_progress((int)std::min(end, text_files.size()),
                        (int)text_files.size(), total_chunks);

    if (aborted) {
      break;
    }
  }

  tui.dismiss_progress_modal();

  // Finalize the .rag file header with the real chunk count
  RagDB::finalize_file(rag_file, (uint32_t)total_chunks);

  // Clean up extraction directory and cache
  fs::remove_all(extract_dir);
  g_case_cache.clear();

  if (aborted) {
    fs::remove(rag_file);
    fail("embedding aborted by user");
    return false;
  }

  if (total_chunks == 0) {
    fs::remove(rag_file);
    fail("no chunks produced");
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim",
              "build_from_path stats: %zu files | resolved: %d redirects | "
              "skipped: %d unreadable, %d short-raw, %d redirect, "
              "%d short-stripped, %d no-chunks, %d duplicate | %d chunks produced",
              text_files.size(), resolved_redirects,
              skip_unreadable, skip_short_raw,
              skip_redirect, skip_short_stripped,
              skip_no_chunks, skip_duplicate, total_chunks);
  }

  if (progress) {
    progress(std::format(
                         "index stats: {} files | {} redirects resolved | "
                         "skipped: {} unreadable, {} short-raw, {} redirect, "
                         "{} short-stripped, {} no-chunks, {} duplicate | {} chunks produced",
                         text_files.size(), resolved_redirects,
                         skip_unreadable, skip_short_raw,
                         skip_redirect, skip_short_stripped,
                         skip_no_chunks, skip_duplicate, total_chunks));
  }

  // Phase 3: Update index
  BuildStats bstats;
  bstats.total_files         = (int)text_files.size();
  bstats.resolved_redirects  = resolved_redirects;
  bstats.skip_unreadable     = skip_unreadable;
  bstats.skip_short_raw      = skip_short_raw;
  bstats.skip_redirect       = skip_redirect;
  bstats.skip_short_stripped = skip_short_stripped;
  bstats.skip_no_chunks      = skip_no_chunks;
  bstats.skip_duplicate      = skip_duplicate;
  return zim_save_and_index(id, zim_path, total_chunks, bstats, progress);
}
