// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// MTP speculative decoding, one target pass per round.
//
// Terminology
//   anchor  : a token already sampled (and accepted by _sampler) but NOT yet decoded.
//             Its position is P = _tokens_physically_used.
//   _pending_h : nextn hidden state of the last *committed* position (P - 1).
//
// One round:
//   1. draft   : MTP(anchor, _pending_h) -> d0, MTP(d0, h') -> d1 ...
//   2. verify  : ONE target decode of [anchor, d0 .. d(k-1)] at P .. P+k
//                row j of the output predicts the token at P+j+1
//   3. accept  : sample row j with the real sampler; keep going while it equals d_j.
//                the first sample that differs (or the one from row k) becomes
//                the next anchor. It is NOT decoded now, it heads the next batch.
//   4. rollback: drop rejected positions from the target KV (and recurrent state)
//   5. resync  : rebuild the MTP KV for the committed positions from target hidden states
//
// MTP convention (matches sync_and_capture_mtp): the MTP KV entry at position p is
// built from (token_p, target_hidden_{p-1}) and its output predicts token_{p+1}.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "llama.h"
#include "llama_sb.h"
#include "llama-ext.h"
#include "logging.h"

#if !defined(LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY)
#error "llama.h has no LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY: needed to snapshot recurrent (Gated DeltaNet) state for rollback"
#endif

namespace {

constexpr uint32_t CKPT_FLAGS = LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY;

// how rejected draft positions are removed from the target memory
constexpr int ROLLBACK_UNKNOWN = 0;  // not probed yet: snapshot every round until the first rejection
constexpr int ROLLBACK_NATIVE = 1;   // llama_memory_seq_rm can rewind everything: no snapshots needed
constexpr int ROLLBACK_CKPT = 2;     // recurrent state cannot be rewound: snapshot / restore / re-decode

// feed target hidden states into the MTP context while processing the prompt as well
// (the old code only did so after the first call). Set to false to restore the old behaviour.
constexpr bool MTP_INJECT_PROMPT_HIDDEN = true;

// RAII wrapper: llama_batch_init() allocates seq_id[i] and the rest, llama_batch_free() releases them.
struct DecodeBatch {
  llama_batch b;
  explicit DecodeBatch(int capacity) : b(llama_batch_init(capacity, 0, 1)) { b.n_tokens = 0; }
  ~DecodeBatch() { llama_batch_free(b); }
  DecodeBatch(const DecodeBatch &) = delete;
  DecodeBatch &operator=(const DecodeBatch &) = delete;

  void add(llama_token tok, llama_pos pos) {
    const int i = b.n_tokens++;
    b.token[i] = tok;
    b.pos[i] = pos;
    b.n_seq_id[i] = 1;
    b.seq_id[i][0] = 0;  // NB: the old code assigned the POINTER (seq_id[i] = 0) -> null deref
    b.logits[i] = 1;     // outputs (logits + nextn hidden) for every row
  }
};

// greedy pick + optional probability of the top token (full softmax denominator)
llama_token top1(const float *logits, int n, bool want_prob, float &prob) {
  int best = 0;
  float mx = logits[0];
  for (int i = 1; i < n; ++i) {
    if (logits[i] > mx) {
      mx = logits[i];
      best = i;
    }
  }
  prob = 1.0f;
  if (want_prob) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
      sum += std::exp(logits[i] - mx);
    }
    prob = 1.0f / sum;
  }
  return (llama_token)best;
}

}  // namespace

// ---------------------------------------------------------------------------
// MTP KV sync: feed `tokens` (already decoded on the target, last batch) into the MTP
// context with the matching target hidden states, and remember the last hidden row.
// ---------------------------------------------------------------------------
void Llama::sync_and_capture_mtp(const llama_token *tokens, int n_tokens, llama_pos pos_start) {
  if (!_mtp_enabled || n_tokens <= 0) {
    return;
  }

  const float *h_tgt = llama_get_embeddings_nextn(_ctx);
  if (!h_tgt) {
    log_write(LEVEL_INFO, "HALI: MTP: no nextn embeddings from target");
    return;
  }

  const size_t n_embd = (size_t)_n_embd;
  const bool has_prior = _pending_h.size() == n_embd;

  // hidden state of the position just before this batch (zeros at the very start)
  const vector<float> h_prev = has_prior ? _pending_h : vector<float>(n_embd, 0.0f);

  bool ok = true;
  llama_batch_ext_clear(_batch_mtp);
  for (int k = 0; k < n_tokens && ok; ++k) {
    const int32_t idx = llama_batch_ext_add_token(_batch_mtp, 0, tokens[k]);
    if (idx < 0) {
      log_write(LEVEL_INFO, "HALI: MTP: sync batch full at %d/%d", k, n_tokens);
      ok = false;
      break;
    }
    llama_pos pos = pos_start + k;
    llama_batch_ext_set_pos(_batch_mtp, idx, &pos);
    if (has_prior || MTP_INJECT_PROMPT_HIDDEN) {
      const float *h_row = (k == 0) ? h_prev.data() : h_tgt + (size_t)(k - 1) * n_embd;
      llama_batch_ext_set_embd_state(_batch_mtp, idx, {h_row, 1, n_embd});
    }
  }

  if (ok && llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, _batch_mtp) != 0) {
    log_write(LEVEL_INFO, "HALI: MTP: sync decode failed");
  }

  // always capture target-side state, even if the MTP side failed
  _verify_h.assign(h_tgt, h_tgt + (size_t)n_tokens * n_embd);
  _verify_h_rows = n_tokens;
  _pending_h.assign(h_tgt + (size_t)(n_tokens - 1) * n_embd, h_tgt + (size_t)n_tokens * n_embd);
}

// ---------------------------------------------------------------------------
// Draft up to n_max tokens following `anchor` (which sits at position `pos`).
// ---------------------------------------------------------------------------
vector<llama_token> Llama::generate_draft_tokens(llama_token anchor, llama_pos pos, int n_max) {
  vector<llama_token> drafts;
  if (!_mtp_enabled || n_max <= 0 || _pending_h.size() != (size_t)_n_embd) {
    return drafts;
  }

  const int n_vocab = llama_vocab_n_tokens(_vocab);
  vector<float> h = _pending_h;  // hidden of position pos-1
  llama_token tok = anchor;

  for (int i = 0; i < n_max; ++i) {
    llama_batch_ext_clear(_batch_mtp);
    const int32_t idx = llama_batch_ext_add_token(_batch_mtp, 0, tok);
    if (idx < 0) {
      break;
    }
    llama_pos p = pos + i;
    llama_batch_ext_set_pos(_batch_mtp, idx, &p);
    llama_batch_ext_set_embd_state(_batch_mtp, idx, {h.data(), 1, (size_t)_n_embd});
    llama_batch_ext_set_output_logits(_batch_mtp, idx, true);

    if (llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, _batch_mtp) != 0) {
      break;
    }

    const float *logits = llama_get_logits_ith(_ctx_mtp, -1);
    if (!logits) {
      break;
    }

    // the first draft is always tried: verifying one more row is free (memory bound).
    // later drafts cost an extra MTP pass and a verify row, so gate them on confidence.
    float prob = 1.0f;
    const bool gate = (i > 0 && _mtp_p_min > 0.0f);
    const llama_token next = top1(logits, n_vocab, gate, prob);
    if (llama_vocab_is_eog(_vocab, next) || (gate && prob < _mtp_p_min)) {
      break;
    }
    drafts.push_back(next);

    if (i + 1 < n_max) {
      const float *h_row = llama_get_embeddings_nextn_ith(_ctx_mtp, 0);
      if (!h_row) {
        break;
      }
      std::memcpy(h.data(), h_row, (size_t)_n_embd * sizeof(float));
    }
    tok = next;
  }
  return drafts;
}

// ---------------------------------------------------------------------------
// One target pass over [anchor, drafts...], accept the matching prefix.
//   accepted : the accepted draft tokens (NOT including anchor)
//   next     : the next anchor (sampled, accepted by _sampler, NOT decoded)
// On success the target holds exactly anchor + accepted, _tokens_physically_used is
// advanced by accepted.size() + 1 and the MTP context is re-synced.
// ---------------------------------------------------------------------------
bool Llama::verify_and_accept(llama_token anchor, const vector<llama_token> &drafts,
                              vector<llama_token> &accepted, llama_token &next) {
  accepted.clear();
  const int k = (int)drafts.size();
  const llama_pos P = (llama_pos)_tokens_physically_used;
  llama_memory_t mem = llama_get_memory(_ctx);

  // snapshot the recurrent part of the state until we know seq_rm can rewind it by itself
  if (k > 0 && _rollback_mode != ROLLBACK_NATIVE) {
    const size_t sz = llama_state_seq_get_size_ext(_ctx, 0, CKPT_FLAGS);
    _ckpt.resize(sz);
    if (sz > 0 && llama_state_seq_get_data_ext(_ctx, _ckpt.data(), sz, 0, CKPT_FLAGS) != sz) {
      log_write(LEVEL_INFO, "HALI: MTP: state snapshot failed");
      _ckpt.clear();
    }
    if (_rollback_mode == ROLLBACK_UNKNOWN) {
      log_write(LEVEL_INFO, "HALI: MTP: rollback probe, snapshot=%zu bytes", sz);
    }
  }

  // ---- the single verification pass ---------------------------------------
  {
    DecodeBatch b(k + 1);
    b.add(anchor, P);
    for (int i = 0; i < k; ++i) {
      b.add(drafts[i], P + 1 + i);
    }
    const int rc = llama_decode(_ctx, b.b);
    if (rc != 0) {
      log_write(LEVEL_INFO, "HALI: MTP: verify decode failed rc=%d (k=%d pos=%d)", rc, k, (int)P);
      if (k > 0) {
        llama_memory_seq_rm(mem, 0, P, -1);
      }
      return false;
    }
  }

  // ---- accept: sample with the real sampler row by row ---------------------
  // row j predicts the token at P+j+1; row k is the bonus row.
  int n_acc = 0;
  llama_token sampled = LLAMA_TOKEN_NULL;
  for (int j = 0; j <= k; ++j) {
    sampled = llama_sampler_sample(_sampler, _ctx, j);
    if (j == k || sampled != drafts[j]) {
      if (j < k) {
        char dt[64] = {0}, st[64] = {0};
        llama_token_to_piece(_vocab, drafts[j], dt, sizeof(dt) - 1, 0, false);
        llama_token_to_piece(_vocab, sampled, st, sizeof(st) - 1, 0, false);
        const float *row = llama_get_logits_ith(_ctx, j);
        if (row) {
          const int nv = llama_vocab_n_tokens(_vocab);
          int top[5] = {0,0,0,0,0};
          float tv[5] = {-1e30f,-1e30f,-1e30f,-1e30f,-1e30f};
          int rank = 0;
          for (int t = 0; t < nv; ++t) {
            if (t != (int)drafts[j] && row[t] > row[drafts[j]]) {
              ++rank;
            }
            for (int s = 0; s < 5; ++s) {
              if (row[t] > tv[s]) {
                for (int m = 4; m > s; --m) {
                  tv[m] = tv[m-1];
                  top[m] = top[m-1];
                }
                tv[s] = row[t];
                top[s] = t;
                break;
              }
            }
          }
          log_write(LEVEL_DEBUG,
            "HALI: MTP reject j=%d: draft=%d(\"%s\") sampled=%d(\"%s\") "
            "draft_logit=%.3f rank=%d "
            "top5=[%d:%.3f %d:%.3f %d:%.3f %d:%.3f %d:%.3f]",
            j, (int)drafts[j], dt, (int)sampled, st,
            row[drafts[j]], rank,
            top[0], tv[0], top[1], tv[1], top[2], tv[2], top[3], tv[3], top[4], tv[4]);
        } else {
          log_write(LEVEL_DEBUG, "HALI: MTP reject j=%d: draft=%d sampled=%d (no logits)",
                    j, (int)drafts[j], (int)sampled);
        }
      }
      break;
    }
    accepted.push_back(drafts[j]);
    ++n_acc;
  }
  next = sampled;

  // ---- rollback of rejected positions --------------------------------------
  if (n_acc < k) {
    const llama_pos keep_end = P + n_acc + 1;  // [P, keep_end) stays committed
    if (llama_memory_seq_rm(mem, 0, keep_end, -1)) {
      if (_rollback_mode == ROLLBACK_UNKNOWN) {
        log_write(LEVEL_INFO, "HALI: MTP: seq_rm rewinds the whole state, snapshots disabled");
        _rollback_mode = ROLLBACK_NATIVE;
        _ckpt.clear();
        _ckpt.shrink_to_fit();
      }
    } else {
      // recurrent layers cannot be rewound: restore the pre-verify state and re-decode the accepted prefix
      if (_ckpt.empty()) {
        log_write(LEVEL_INFO, "HALI: MTP: seq_rm failed and no snapshot is available, disabling MTP");
        _mtp_enabled = false;
        return false;
      }
      if (_rollback_mode != ROLLBACK_CKPT) {
        log_write(LEVEL_INFO, "HALI: MTP: partial seq_rm unsupported, using snapshot/restore (snapshot %zu bytes/round)",
                  _ckpt.size());
        _rollback_mode = ROLLBACK_CKPT;
      }
      llama_memory_seq_rm(mem, 0, P, -1);  // attention KV back to before the round
      if (llama_state_seq_set_data_ext(_ctx, _ckpt.data(), _ckpt.size(), 0, CKPT_FLAGS) == 0) {
        log_write(LEVEL_INFO, "HALI: MTP: state restore failed, disabling MTP");
        _mtp_enabled = false;
        return false;
      }
      DecodeBatch rb(n_acc + 1);
      rb.add(anchor, P);
      for (int i = 0; i < n_acc; ++i) {
        rb.add(drafts[i], P + 1 + i);
      }
      if (llama_decode(_ctx, rb.b) != 0) {
        log_write(LEVEL_INFO, "HALI: MTP: re-decode after restore failed");
        _mtp_enabled = false;
        return false;
      }
    }
  }

  _tokens_physically_used += n_acc + 1;

  // ---- resync the MTP context for the committed positions -------------------
  // draft-time MTP entries (built from MTP's own hidden states) are replaced by ones
  // built from the target's hidden states, which keeps later acceptance high.
  if (_mtp_enabled && n_acc < k) {
    llama_memory_seq_rm(llama_get_memory(_ctx_mtp), 0, P, -1);
    vector<llama_token> committed;
    committed.reserve(n_acc + 1);
    committed.push_back(anchor);
    committed.insert(committed.end(), drafts.begin(), drafts.begin() + n_acc);
    sync_and_capture_mtp(committed.data(), n_acc + 1, P);
  }

  static uint64_t s_rounds = 0, s_drafted = 0, s_accepted = 0;
  ++s_rounds;
  s_drafted += k;
  s_accepted += n_acc;
  log_write(LEVEL_DEBUG, "HALI: MTP: %d/%d accepted", n_acc, k);
  if (s_rounds % 128 == 0) {
    log_write(LEVEL_INFO, "HALI: MTP: %llu rounds, %.2f tokens/round, draft acceptance %.0f%%",
              (unsigned long long)s_rounds, 1.0 + (double)s_accepted / s_rounds,
              s_drafted ? 100.0 * s_accepted / s_drafted : 0.0);
  }
  return true;
}

// ---------------------------------------------------------------------------
// draft + verify
// ---------------------------------------------------------------------------
bool Llama::mtp_round(llama_token anchor, vector<llama_token> &accepted, llama_token &next) {
  const llama_pos P = (llama_pos)_tokens_physically_used;
  const int room = (int)llama_n_ctx(_ctx) - (int)P - 1;
  const int limit = std::min({_mtp_n_max, (int)llama_n_batch(_ctx) - 1, room});
  vector<llama_token> drafts;
  if (limit > 0) {
    drafts = generate_draft_tokens(anchor, P, limit);
  }
  return verify_and_accept(anchor, drafts, accepted, next);
}

// ---------------------------------------------------------------------------
// wipe everything MTP related (after a context flush / reset)
// ---------------------------------------------------------------------------
void Llama::mtp_reset_state() {
  if (_ctx_mtp) {
    llama_memory_clear(llama_get_memory(_ctx_mtp), true);
  }
  _pending_h.clear();
  _verify_h.clear();
  _verify_h_rows = 0;
  _mtp_buffer.clear();
  _has_pending = false;
}
