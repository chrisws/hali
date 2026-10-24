// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// SuffixDrafter - lightweight n-gram suffix drafter (plan 08)
//
// Builds a hash table mapping n-grams of tokens to the token that
// follows them.  At draft time the last N tokens are looked up and the
// continuation is returned, chained up to max_draft tokens deep.

#include <vector>
#include "llama.h"
#include "llama_sb_suffix.h"

// FNV-1a constants (64-bit)
static constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
static constexpr uint64_t FNV_PRIME  = 0x100000001b3ULL;

void SuffixDrafter::init(int max_ngram, int max_entries) {
  max_ngram_  = max_ngram;
  max_entries_ = max_entries;
  table_.clear();
  table_.reserve(max_entries_);
}

uint64_t SuffixDrafter::hash_ngram(const llama_token *tokens, int n) {
  uint64_t hash = FNV_OFFSET;
  for (int i = 0; i < n; ++i) {
    hash ^= (uint64_t)tokens[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

void SuffixDrafter::add_context(const llama_token *tokens, int n) {
  if (n <= max_ngram_) {
    return;
  }
  for (int i = max_ngram_; i < n; ++i) {
    uint64_t key = hash_ngram(tokens + i - max_ngram_, max_ngram_);
    // Keep the first occurrence (most common continuation wins)
    if (table_.find(key) == table_.end()) {
      table_[key] = {tokens[i]};
    }
  }
  // Soft cap: if we exceed max_entries, stop growing
  // (no eviction; the table is cleared on context reset)
}

std::vector<llama_token> SuffixDrafter::draft(const llama_token *recent, int ngram_len,
                                              int max_draft) const {
  std::vector<llama_token> result;
  if (ngram_len <= 0 || ngram_len > max_ngram_ || max_draft <= 0) {
    return result;
  }

  // Working window: [recent[0..ngram_len-1], draft slots...]
  std::vector<llama_token> window(ngram_len + max_draft);
  for (int i = 0; i < ngram_len; ++i) {
    window[i] = recent[i];
  }

  for (int d = 0; d < max_draft; ++d) {
    // Slide the window: the ngram starts at offset d
    uint64_t key = hash_ngram(window.data() + d, ngram_len);
    auto it = table_.find(key);
    if (it == table_.end()) {
      break;
    }
    window[ngram_len + d] = it->second[0];
    result.push_back(it->second[0]);
  }

  return result;
}

void SuffixDrafter::clear() {
  table_.clear();
}