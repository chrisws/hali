// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// MTP speculative decoding via llama.cpp's common_speculative API.
//
// One round:
//   1. draft   : common_speculative_draft generates draft tokens
//   2. verify  : ONE target decode of [anchor, d0 .. d(k-1)] at P .. P+k
//   3. accept  : common_sampler_sample_and_accept_n samples each row,
//                keeps the matching prefix, the first mismatch becomes next anchor
//   4. rollback: drop rejected positions from the target KV
//   5. resync  : common_speculative_process keeps the MTP context in sync

#include <algorithm>
#include <vector>
#include "llama.h"
#include "llama_sb.h"
#include "llama-ext.h"
#include "logging.h"
#include "common.h"
#include "speculative.h"
#include "sampling.h"

// ---------------------------------------------------------------------------
// Build a common_batch from a token range for use with common_speculative_process
// ---------------------------------------------------------------------------
static common_batch make_common_batch(llama_context *ctx, const llama_token *tokens, int n_tokens, llama_pos pos_start) {
  common_batch batch(ctx);
  for (int i = 0; i < n_tokens; ++i) {
    batch.add(tokens[i], pos_start + i, 0, true);
  }
  return batch;
}

// ---------------------------------------------------------------------------
// Feed a decoded batch to the speculative context (replaces sync_and_capture_mtp)
// ---------------------------------------------------------------------------
void Llama::sync_and_capture_mtp(const llama_token *tokens, int n_tokens, llama_pos pos_start) {
  if (!_mtp_enabled || !_spec || n_tokens <= 0) {
    return;
  }
  mtp_trim_draft(pos_start);
  common_batch batch = make_common_batch(_ctx, tokens, n_tokens, pos_start);
  if (!common_speculative_process(_spec.get(), batch)) {
    log_write(LEVEL_INFO, "HALI: MTP: speculative process failed: %s", _last_error.c_str());
    auto * ctx_dft = _spec_init ? _spec_init->context() : nullptr;
    if (ctx_dft) {
      const llama_pos dft_pos_min = llama_memory_seq_pos_min(llama_get_memory(ctx_dft), 0);
      const llama_pos dft_pos_max = llama_memory_seq_pos_max(llama_get_memory(ctx_dft), 0);
      log_write(LEVEL_INFO, "HALI: MTP: ctx_dft pos range [%d, %d], batch pos [%d, %d]",
                (int)dft_pos_min, (int)dft_pos_max, (int)pos_start, (int)(pos_start + n_tokens - 1));
    }
    _last_error.clear();
  }
}

// ---------------------------------------------------------------------------
// Drop draft-context KV entries at positions >= from, so process()/draft()
// can rewrite them (llama requires strictly increasing positions per sequence)
// ---------------------------------------------------------------------------
void Llama::mtp_trim_draft(llama_pos from) {
  auto *ctx_dft = _spec_init ? _spec_init->context() : nullptr;
  if (ctx_dft) {
    llama_memory_seq_rm(llama_get_memory(ctx_dft), 0, from, -1);
  }
}

// ---------------------------------------------------------------------------
// One speculative round: draft + verify + accept
// ---------------------------------------------------------------------------
bool Llama::mtp_round(llama_token anchor, vector<llama_token> &accepted, llama_token &next) {
  accepted.clear();
  const llama_pos P = (llama_pos)_tokens_physically_used;
  const int room = (int)llama_n_ctx(_ctx) - (int)P - 1;
  const int limit = std::min({_mtp_n_max, (int)llama_n_batch(_ctx) - 1, room});
  if (limit <= 0) {
    return false;
  }

  // ---- 1. generate draft tokens -------------------------------------------
  llama_tokens draft;
  {
    auto &dp = common_speculative_get_draft_params(_spec.get(), 0);
    dp.drafting = true;
    dp.n_max = limit;
    dp.pos0 = P;
    dp.id_last = anchor;
    dp.result = &draft;
    common_speculative_draft(_spec.get());
  }

  const int k = (int)draft.size();
  if (k == 0) {
    // no drafts generated: just decode the anchor normally
    // (drafting may have left speculative entries in ctx_dft, clear them
    // so the decode_anchor -> process() call can write position P)
    mtp_trim_draft(P);
    return false;
  }

  // ---- 2. verify: single target pass over [anchor, d0..d(k-1)] ------------
  {
    const int n = 1 + k;
    vector<llama_token>   toks(n);
    vector<llama_pos>     pos(n);
    vector<int32_t>       n_seq(n, 1);
    vector<llama_seq_id>  seq_ids(n, 0);
    vector<llama_seq_id*> seq_ptrs(n);
    vector<int8_t>        logits(n, 1);

    toks[0] = anchor;
    pos[0]  = P;
    for (int i = 0; i < k; ++i) {
      toks[i + 1] = draft[i];
      pos[i + 1]  = P + 1 + i;
    }
    for (int i = 0; i < n; ++i) {
      seq_ptrs[i] = &seq_ids[i];
    }

    llama_batch batch = {
      n,
      toks.data(),
      nullptr,
      pos.data(),
      n_seq.data(),
      seq_ptrs.data(),
      logits.data()
    };

    const int rc = llama_decode(_ctx, batch);
    if (rc != 0) {
      log_write(LEVEL_INFO, "HALI: MTP: verify decode failed rc=%d (k=%d pos=%d)", rc, k, (int)P);
      llama_memory_seq_rm(llama_get_memory(_ctx), 0, P, -1);
      mtp_trim_draft(P);
      return false;
    }
  }

  // ---- 3. feed the verify batch to the speculative context ----------------
  // drafting wrote speculative entries at P.. into ctx_dft: remove them first,
  // otherwise process() fails with "inconsistent sequence positions"
  mtp_trim_draft(P);
  {
    common_batch batch_tgt(_ctx);
    batch_tgt.add(anchor, P, 0, true);
    for (int i = 0; i < k; ++i) {
      batch_tgt.add(draft[i], P + 1 + i, 0, true);
    }
    if (!common_speculative_process(_spec.get(), batch_tgt)) {
      log_write(LEVEL_INFO, "HALI: MTP: speculative process (verify) failed: %s, disabling MTP", _last_error.c_str());
      _last_error.clear();
      // the draft head is now out of sync with the target: stop using it.
      // this round's verify results are still valid, so finish it below.
      _mtp_enabled = false;
    }
  }

  // ---- 4. sample and accept ------------------------------------------------
  // common_sampler_sample_and_accept_n returns at least 1 token, up to k+1.
  // ids[0..n_acc-1] are the accepted draft tokens, ids.back() is the next token
  // (either the mismatch correction or the bonus token after a full accept).
  auto ids = common_sampler_sample_and_accept_n(_smpl_mtp, _ctx, draft);

  const int n_acc = (int)ids.size() - 1;  // number of draft tokens accepted
  next = ids.back();

  for (int i = 0; i < n_acc; ++i) {
    accepted.push_back(ids[i]);
  }

  // ---- 5. inform the speculative context of the acceptance ----------------
  common_speculative_accept(_spec.get(), 0, (uint16_t)n_acc);

  // ---- 6. rollback rejected positions --------------------------------------
  if (n_acc < k) {
    const llama_pos keep_end = P + n_acc + 1;  // [P, keep_end) stays committed
    mtp_trim_draft(keep_end);  // drop rejected rows from the draft cache too
    if (!llama_memory_seq_rm(llama_get_memory(_ctx), 0, keep_end, -1)) {
      log_write(LEVEL_INFO, "HALI: MTP: seq_rm failed, disabling MTP");
      _mtp_enabled = false;
      return false;
    }
  }

  _tokens_physically_used += n_acc + 1;

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
// wipe everything MTP related (after a context flush / reset)
// ---------------------------------------------------------------------------
void Llama::mtp_reset_state() {
  if (!_spec) {
    _mtp_buffer.clear();
    _has_pending = false;
    return;
  }

  // free the speculative context and its draft context
  _spec.reset();
  _spec_init.reset();

  // re-create
  _spec_params.speculative.draft.ctx_tgt = _ctx;
  _spec_init = common_speculative_init_from_params(_spec_params, _model, _ctx);
  if (_spec_init) {
    _spec_params.speculative.draft.ctx_dft = _spec_init->context();
    _spec.reset(common_speculative_init(_spec_params.speculative, 1));
  }

  _mtp_buffer.clear();
  _has_pending = false;
}

bool Llama::configure_mtp_sampler() {
  if (_mtp_enabled) {
    if (_smpl_mtp) {
      common_sampler_free(_smpl_mtp);
    }

    const bool greedy = _temperature <= 0.0f;
    // defaults are NOT neutral (top_k=40, top_p=0.95, ...)
    common_params_sampling sp;
    sp.seed             = _seed;
    sp.temp             = _temperature;
    sp.top_k            = greedy ? 0 : _top_k;
    sp.top_p            = greedy ? 1.0f : _top_p;
    sp.min_p            = greedy ? 0.0f : _min_p;
    sp.penalty_last_n   = _penalty_last_n;
    sp.penalty_repeat   = _penalty_repeat;
    sp.penalty_freq     = _penalty_freq;
    sp.penalty_present  = _penalty_present;
    //sp.grammar          = _grammar_src.c_str();
    _smpl_mtp = common_sampler_init(_model, sp);
    if (!_smpl_mtp) {
      set_last_error("failed to initialize MTP sampler");
      return false;
    }
  }
  return true;
}

bool Llama::decode_anchor(llama_token tok) {
  llama_batch batch = llama_batch_get_one(&tok, 1);
  if (llama_decode(_ctx, batch)) {
    return false;
  }
  _tokens_physically_used += 1;
  sync_and_capture_mtp(&tok, 1, (llama_pos)(_tokens_physically_used - 1));
  return true;
}

string Llama::emit_token(LlamaIter &iter, llama_token tok) {
  if (llama_vocab_is_eog(_vocab, tok)) {
    iter._has_next = false;
    _mtp_buffer.clear();
    _has_pending = false;
    return "";
  }
  string result = token_to_string(iter, tok);
  if (!iter._has_next) {
    // iteration over (stop word / max tokens): the last emitted token is the undecoded anchor,
    // put it in the KV so the conversation continues from a consistent state
    const bool is_anchor = _has_pending && _mtp_buffer.empty() && tok == _anchor;
    _mtp_buffer.clear();
    _has_pending = false;
    if (is_anchor) {
      decode_anchor(tok);
    }
  }
  return result;
}

