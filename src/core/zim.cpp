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
#include <thread>
#include <atomic>
#include <mutex>
#include <semaphore>
#include <unordered_map>

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
                                  Tui &tui,
                                  const ProgressFn &progress,
                                  std::string &extract_dir) {
  extract_dir = "/tmp/hali_zim_" + id;
  if (fs::exists(extract_dir)) {
    run_with_spinner(tui, [&]() -> std::string {
      fs::remove_all(extract_dir);
      return "";
    }, "cleaning up previous extraction");
  }
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
    run_with_spinner(tui, [&]() -> std::string {
      for (const auto &entry : fs::recursive_directory_iterator(extract_dir)) {
        if (entry.is_regular_file()) {
          disk_count++;
        }
      }
      return "";
    }, std::format("counting extracted files"));
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
    target_path = resolve_case_insensitive(fs::path(utils::join_path(target_path.parent_path().string(),
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

std::vector<std::pair<std::string, std::string>>
  KnowledgeManager::zim_read_chunks(const std::vector<std::filesystem::path> &text_files,
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
    const int nthreads = std::max(1u, std::thread::hardware_concurrency() / 2);

    if (progress) {
      progress(std::format("reading {} files with {} threads ...",
                           text_files.size(), nthreads));
    }

    std::counting_semaphore io_sem(4);

    auto worker = [&]() {
      const std::filesystem::path base_dir(extract_dir);
      std::vector<ChunkData> local;

      while (true) {
        int idx = read_idx.fetch_add(1);
        if (idx >= (int)text_files.size()) {
          break;
        }
        const auto &tf = text_files[idx];
        io_sem.acquire();
        std::ifstream f(tf);
        if (!f) {
          io_sem.release();
          continue;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        f.close();
        io_sem.release();
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
        local.push_back({std::move(content), tf.filename().string()});
      }

      if (!local.empty()) {
        std::lock_guard<std::mutex> lock(chunk_mutex);
        chunks.insert(chunks.end(),
                      std::make_move_iterator(local.begin()),
                      std::make_move_iterator(local.end()));
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

int KnowledgeManager::zim_embed_chunks(const std::vector<std::pair<std::string, std::string>> &chunks,
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
