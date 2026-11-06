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

// ============================================================================
// Static / free helpers
// ============================================================================

//
// Fast platform-aware directory removal.
// Significantly faster than fs::remove_all for large trees of small
// files (e.g. ZIM extractions with thousands of articles).
// Safety: only removes paths under .hali/tmp/.
//
static void shell_remove_all(const std::string &path) {
  if (path.empty() || !fs::exists(path)) {
    return;
  }
  // Guard: refuse to remove anything outside .hali/tmp/
  if (path.rfind(".hali/tmp/", 0) != 0) {
    log_write(LEVEL_WARNING, "zim",
              "shell_remove_all: refusing path outside .hali/tmp/: %s",
              path.c_str());
    return;
  }
  switch (current_platform()) {
    case Platform::Linux:
    case Platform::macOS:
      run_shell_command(std::format("rm -rf '{}'", path));
      break;
    case Platform::Windows:
      run_shell_command(std::format("rmdir /s /q \"{}\"", path));
      break;
    default:
      fs::remove_all(path);
      break;
  }
}

static std::string zim_extract_dir(const std::string &id) {
  return ".hali/tmp/hali_zim_" + id;
}

//
// Thread-safe cache for case-insensitive directory lookups.
// Each directory is scanned at most once; subsequent lookups are O(1).
//
struct CaseInsensitiveCache {
  std::mutex mutex;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, fs::path>> dirs;
  size_t total_entries = 0;
  static constexpr size_t MAX_ENTRIES = 200000;

  void clear() {
    std::lock_guard<std::mutex> lock(mutex);
    dirs.clear();
    total_entries = 0;
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

    // Clear cache before obtaining reference to avoid dangling ref
    if (total_entries > MAX_ENTRIES) {
      dirs.clear();
      total_entries = 0;
    }

    auto &dir_map = dirs[key];
    if (dir_map.empty()) {
      std::error_code ec;
      if (fs::is_directory(parent, ec)) {
        for (const auto &entry : fs::directory_iterator(parent, ec)) {
          if (entry.is_regular_file()) {
            dir_map[to_lower(entry.path().filename().string())] =
              entry.path();
            total_entries++;
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
std::string url_decode(const std::string &encoded) {
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
bool is_redirect_stub(const std::string &content) {
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

//
// Load a topic filter file. One keyword per line or comma-separated.
// Skips blank lines and # comments. Returns empty vector if file missing.
//
std::vector<std::string> load_filter_file(const std::string &path) {
  std::vector<std::string> keywords;
  std::ifstream f(path);
  if (!f) {
    return keywords;
  }
  std::string line;
  while (std::getline(f, line)) {
    line = utils::trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t pos = 0;
    while ((pos = line.find(',')) != std::string::npos) {
      auto kw = utils::trim(line.substr(0, pos));
      if (!kw.empty()) {
        keywords.push_back(kw);
      }
      line.erase(0, pos + 1);
    }
    auto kw = utils::trim(line);
    if (!kw.empty()) {
      keywords.push_back(kw);
    }
  }
  return keywords;
}

//
// Normalize a string into a vector of lowercase alphanumeric word tokens.
// All non-alphanumeric bytes (underscores, hyphens, dots, colons, commas,
// parentheses, Unicode multi-byte sequences, etc.) act as delimiters.
//
//   "Half-Life_2:_Episode_One"  -> ["half","life","2","episode","one"]
//   "Broad_Street_(England)"    -> ["broad","street","england"]
//   "F*_(programming_language)" -> ["f","programming","language"]
//   "Made_in_the_A.M."          -> ["made","in","the","a","m"]
//   "house cat"                 -> ["house","cat"]
//
static std::vector<std::string> normalize_to_words(const std::string &s) {
  std::vector<std::string> tokens;
  std::string current;
  for (unsigned char c : s) {
    if (std::isalnum(c)) {
      current += static_cast<char>(std::tolower(c));
    } else {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    }
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

//
// Check if a single title word matches a keyword part:
// exact match or prefix match.
//
static bool word_matches(const std::string &word, const std::string &part) {
  return word == part;
}

//
// Check if an article title matches any keyword in the filter list.
// Both the title and each keyword are normalised to lowercase alphanumeric
// word tokens (all punctuation, underscores, Unicode, etc. are delimiters).
// A keyword matches when its word tokens appear as a contiguous subsequence
// in the title's word tokens, with per-word prefix matching.
//
//   "health"       matches "Health", "Healthcare", "Public_health_systems"
//   "house cat"    matches "House_cat", "The_house_cat_of_Maine"
//   "half-life"    matches "Half-Life_2:_Episode_One"
//   "cat breed"    matches "Cat_breeds"
//   "unhealthy"    does NOT match "Health"
//
bool title_matches_filter(const std::string &filename,
                          const std::vector<std::string> &keywords) {
  std::vector<std::string> words = normalize_to_words(filename);
  if (words.empty()) {
    return false;
  }

  for (const auto &kw : keywords) {
    std::vector<std::string> kw_parts = normalize_to_words(kw);
    if (kw_parts.empty()) {
      continue;
    }
    if (kw_parts.size() > words.size()) {
      continue;
    }
    for (size_t i = 0; i + kw_parts.size() <= words.size(); i++) {
      bool all_match = true;
      for (size_t j = 0; j < kw_parts.size(); j++) {
        if (!word_matches(words[i + j], kw_parts[j])) {
          all_match = false;
          break;
        }
      }
      if (all_match) {
        return true;
      }
    }
  }
  return false;
}

// ============================================================================
// KnowledgeManager methods
// ============================================================================

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
  extract_dir = zim_extract_dir(id);
  if (fs::exists(extract_dir)) {
    shell_remove_all(extract_dir);
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
    shell_remove_all(extract_dir);
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
// Phase 1: Extract ZIM files, reusing a previous extraction if the
// user declines to overwrite.
// Returns the number of files on disk (0 on failure).
//
int KnowledgeManager::extract_zim_files(const std::string &zim_path,
                                        const std::string &id,
                                        Tui &tui,
                                        const ProgressFn &progress,
                                        BuildContext &ctx) {
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "extract_zim_files: id=%s path=%s",
              id.c_str(), zim_path.c_str());
  }
  const std::string extract_dir = zim_extract_dir(id);
  int disk_count = 0;

  // Check if a previous extraction already exists on disk
  bool reuse_existing = false;
  if (fs::is_directory(extract_dir)) {
    int existing_files = 0;
    for (const auto &entry : fs::recursive_directory_iterator(extract_dir)) {
      if (entry.is_regular_file()) {
        existing_files++;
      }
    }
    if (existing_files > 0) {
      if (progress) {
        progress(std::format("found {} existing files in {}",
                             existing_files, extract_dir));
      }
      bool overwrite = tui.confirm_dialog(std::format("Extracted files exist for '{}'. Overwrite?", id));
      if (!overwrite) {
        reuse_existing = true;
      }
    }
  }

  if (reuse_existing) {
    ctx.extract_dir = extract_dir;
    for (const auto &entry : fs::recursive_directory_iterator(extract_dir)) {
      if (entry.is_regular_file()) {
        disk_count++;
      }
    }
    if (progress) {
      progress(std::format("reusing {} existing files", disk_count));
    }
  } else {
    run_with_spinner(tui, [&]() -> std::string {
      disk_count = zim_extract(zim_path, id, progress, ctx.extract_dir);
      return "";
    });
  }
  return disk_count;
}

//
// Phase 2: Filter text files by topic keywords.
// Modifies text_files in place and updates ctx.skip_filtered.
//
void KnowledgeManager::apply_topic_filter(std::vector<fs::path> &text_files,
                                          const std::string &filter_path,
                                          BuildContext &ctx) const {
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "apply_topic_filter: %zu files, filter=%s",
              text_files.size(), filter_path.c_str());
  }
  if (filter_path.empty()) {
    return;
  }
  auto keywords = load_filter_file(filter_path);
  if (keywords.empty()) {
    return;
  }
  std::vector<fs::path> filtered;
  filtered.reserve(text_files.size());
  for (const auto &f : text_files) {
    if (title_matches_filter(f.filename().string(), keywords)) {
      filtered.push_back(f);
    } else {
      ctx.skip_filtered++;
    }
  }
  text_files = std::move(filtered);
  text_files.shrink_to_fit();
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim",
              "topic filter: %zu keywords, %d files skipped",
              keywords.size(), ctx.skip_filtered);
  }
}

//
// Phase 4: Finalize the rag file, prompt to clear extracted files,
// and check for abort / empty result.
// Returns false if the build should be aborted.
//
bool KnowledgeManager::finalize_build(BuildContext &ctx,
                                      Tui &tui,
                                      const std::string &id,
                                      int disk_count,
                                      const ProgressFn &progress,
                                      std::string *error) const {
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "finalize_build: id=%s files=%d chunks=%d",
              id.c_str(), disk_count, ctx.total_chunks);
  }
  RagDB::finalize_file(ctx.rag_file, ctx.existing_chunks + (uint32_t)ctx.total_chunks);
  g_case_cache.clear();

  // Prompt user to clear extracted files
  if (!ctx.extract_dir.empty() && fs::is_directory(ctx.extract_dir)) {
    bool clear = tui.confirm_dialog(std::format("Clear extracted files for '{}'? ({} files)", id, disk_count));
    if (clear) {
      shell_remove_all(ctx.extract_dir);
    }
  }

  if (ctx.aborted) {
    fs::remove(ctx.rag_file);
    if (progress) {
      progress("embedding aborted by user");
    }
    if (error) {
      *error = "embedding aborted by user";
    }
    return false;
  }

  if (ctx.total_chunks == 0) {
    fs::remove(ctx.rag_file);
    if (progress) {
      progress("no chunks produced");
    }
    if (error) {
      *error = "no chunks produced";
    }
    return false;
  }
  return true;
}

//
// Report build statistics via debug log and progress callback.
//
void KnowledgeManager::report_build_stats(const std::vector<fs::path> &text_files,
                                          const BuildContext &ctx,
                                          const ProgressFn &progress) const {
  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim", "report_build_stats: %zu files, %d chunks",
              text_files.size(), ctx.total_chunks);
    log_write(LEVEL_DEBUG, "zim",
              "build_from_path stats: %zu files | resolved: %d redirects | "
              "skipped: %d unreadable, %d short-raw, %d redirect, "
              "%d short-stripped, %d no-chunks, %d duplicate | %d chunks produced",
              text_files.size(), ctx.resolved_redirects,
              ctx.skip_unreadable, ctx.skip_short_raw,
              ctx.skip_redirect, ctx.skip_short_stripped,
              ctx.skip_no_chunks, ctx.skip_duplicate, ctx.total_chunks);
  }

  if (progress) {
    progress(std::format("index stats: {} files | {} redirects resolved | "
                         "skipped: {} unreadable, {} short-raw, {} redirect, "
                         "{} short-stripped, {} no-chunks, {} duplicate | "
                         "{} chunks produced",
                         text_files.size(), ctx.resolved_redirects,
                         ctx.skip_unreadable, ctx.skip_short_raw,
                         ctx.skip_redirect, ctx.skip_short_stripped,
                         ctx.skip_no_chunks, ctx.skip_duplicate,
                         ctx.total_chunks));
  }
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
                                       std::string *error,
                                       const std::string &filter_path,
                                       bool append) {
  auto fail = [progress, error](const std::string &msg) {
    if (progress) {
      progress(msg);
    }
    if (error) {
      *error = msg;
    }
    log_write(LEVEL_ERROR, "zim", msg.c_str());
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

  BuildContext ctx;
  ctx.id = id;
  ctx.zim_path = zim_path;
  ctx.append = append;
  ctx.rag_file = rag_path(id);

  // Check for existing index
  if (rag_exists(id)) {
    if (!append) {
      if (progress) {
        progress("already indexed: " + id);
      }
      return true;
    }
    {
      std::ifstream hf(ctx.rag_file, std::ios::binary);
      if (hf) {
        /* Validate magic before trusting the chunk count */
        uint32_t magic = 0;
        hf.seekg(0);
        hf.read((char*)&magic, 4);
        if (magic == 0x52414744u) {
          hf.seekg(8);
          hf.read((char*)&ctx.existing_chunks, 4);
        } else {
          ctx.existing_chunks = 0;
        }
      }
      if (progress) {
        progress(std::format("appending to existing index ({} chunks)",
                             ctx.existing_chunks));
      }
    }
  }

  // Phase 1: Extract
  int disk_count = extract_zim_files(zim_path, id, tui, progress, ctx);
  if (disk_count == 0) {
    fail("zimdump produced no files");
    return false;
  }

  auto text_files = collect_text_files(ctx.extract_dir);
  if (text_files.empty()) {
    fail("no text files extracted from ZIM");
    shell_remove_all(ctx.extract_dir);
    return false;
  }

  // Phase 2: Topic filter
  apply_topic_filter(text_files, filter_path, ctx);
  if (text_files.empty()) {
    fail("no files matched topic filter");
    shell_remove_all(ctx.extract_dir);
    return false;
  }

  if (log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "zim",
              "build_from_path: %zu text files found in %s",
              text_files.size(), ctx.extract_dir.c_str());
  }

  if (progress) {
    progress(std::format("found {} text files{} embedding in batches ...",
                         text_files.size(),
                         ctx.skip_filtered > 0
                         ? std::format(" ({} filtered)", ctx.skip_filtered)
                         : ""));
  }

  // Clear case-insensitive cache from any previous build
  g_case_cache.clear();

  // Phase 3: Batch embed with incremental disk writes
  if (!batch_embed(text_files, llama, tui, progress, ctx, error)) {
    shell_remove_all(ctx.extract_dir);
    fs::remove(ctx.rag_file);
    return false;
  }

  // Phase 4: Finalize
  if (!finalize_build(ctx, tui, id, disk_count, progress, error)) {
    return false;
  }

  // Report stats
  report_build_stats(text_files, ctx, progress);

  // Phase 5: Update index
  BuildStats bstats;
  bstats.total_files         = (int)text_files.size();
  bstats.resolved_redirects  = ctx.resolved_redirects;
  bstats.skip_unreadable     = ctx.skip_unreadable;
  bstats.skip_short_raw      = ctx.skip_short_raw;
  bstats.skip_redirect       = ctx.skip_redirect;
  bstats.skip_short_stripped = ctx.skip_short_stripped;
  bstats.skip_no_chunks      = ctx.skip_no_chunks;
  bstats.skip_duplicate      = ctx.skip_duplicate;
  bstats.skip_filtered       = ctx.skip_filtered;
  return zim_save_and_index(id, zim_path,
                            (int)(ctx.existing_chunks + (uint32_t)ctx.total_chunks),
                            bstats, progress);
}

//
// Embed text files in batches, appending chunks to the .rag file
// incrementally to bound memory usage.
//
bool KnowledgeManager::batch_embed(const std::vector<fs::path> &files,
                                   Llama &llama,
                                   Tui &tui,
                                   const ProgressFn &progress,
                                   BuildContext &ctx,
                                   std::string *error) const {
  auto fail = [progress, error](const std::string &msg) {
    if (progress) {
      progress(msg);
    }
    if (error) {
      *error = msg;
    }
    log_write(LEVEL_ERROR, "zim", msg.c_str());  
  };

  constexpr int BATCH_SIZE = 50;
  fs::create_directories(rag_dir_);

  RagDB batch_db;
  batch_db.embed_dim = llama.get_embed_dim();
  if (!ctx.append) {
    if (!batch_db.init_file(ctx.rag_file)) {
      fail("failed to init " + ctx.rag_file);
      return false;
    }
  }

  // Dedup: track canonical content by 64-bit hash of its path.
  // Multiple redirect stubs can point to the same article;
  // we only want to embed each article once.
  std::unordered_set<uint64_t> seen;
  auto path_hash = [](const fs::path &p) -> uint64_t {
    return static_cast<uint64_t>(std::hash<std::string>{}(p.string()));
  };

  tui.show_progress_modal("Embedding", (int)files.size());

  for (size_t i = 0; i < files.size(); i += BATCH_SIZE) {
    const size_t end = std::min(i + (size_t)BATCH_SIZE, files.size());
    for (size_t j = i; j < end; j++) {
      if (tui.poll_escape()) {
        ctx.aborted = true;
        break;
      }

      std::ifstream f(files[j]);
      if (!f) {
        ctx.skip_unreadable++;
        continue;
      }
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      f.close();

      if (content.size() < 80) {
        ctx.skip_short_raw++;
        continue;
      }

      // Determine canonical identity for dedup
      fs::path canonical = files[j];
      if (is_redirect_stub(content)) {
        std::string resolved;
        fs::path resolved_path;
        if (resolve_redirect(content, ctx.extract_dir, resolved, resolved_path)) {
          content = std::move(resolved);
          canonical = resolved_path;
          ctx.resolved_redirects++;
        } else {
          ctx.skip_redirect++;
          continue;
        }
      }

      // Skip if we've already embedded this article
      uint64_t h = path_hash(canonical);
      if (!seen.insert(h).second) {
        ctx.skip_duplicate++;
        continue;
      }

      utils::strip_html(content);
      if (content.size() < 80) {
        ctx.skip_short_stripped++;
        continue;
      }

      int before = batch_db.size();
      llama.rag_index_prose(batch_db, content,
                            files[j].filename().string());
      if (batch_db.size() == before) {
        ctx.skip_no_chunks++;
      }
    }

    log_write(LEVEL_INFO, "zim", "completed next batch");

    // Append this batch's chunks to disk, then free memory
    if (!batch_db.chunks.empty()) {
      const int batch_count = batch_db.size();
      if (!batch_db.append_chunks(ctx.rag_file)) {
        tui.dismiss_progress_modal();
        fail("failed to append chunks to " + ctx.rag_file);
        return false;
      }
      ctx.total_chunks += batch_count;
    }

    tui.update_progress((int)std::min(end, files.size()),
                        (int)files.size(), ctx.total_chunks);

    if (ctx.aborted) {
      break;
    }
  }

  tui.dismiss_progress_modal();
  return true;
}
