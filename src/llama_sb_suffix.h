// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef LLAMA_SB_SUFFIX_H
#define LLAMA_SB_SUFFIX_H

#include <cstdint>
#include <unordered_map>
#include <vector>
#include "llama.h"

//
// SuffixDrafter - lightweight n-gram suffix drafter (plan 08)
//
// Matches the last N tokens against earlier context and drafts the
// continuation. Cheap hash-table lookup; helps with repetitive/structured
// text when MTP acceptance is low.
//
class SuffixDrafter {
public:
  void init(int max_ngram = 4, int max_entries = 65536);
  void add_context(const llama_token *tokens, int n);
  std::vector<llama_token> draft(const llama_token *recent, int ngram_len,
                                  int max_draft) const;
  void clear();
  size_t size() const { return table_.size(); }

private:
  static uint64_t hash_ngram(const llama_token *tokens, int n);

  std::unordered_map<uint64_t, std::vector<llama_token>> table_;
  int max_ngram_ = 4;
  int max_entries_ = 65536;
};

#endif