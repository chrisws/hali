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
// MTP detection & speculative context setup
// ---------------------------------------------------------------------------
void Llama::mtp_init(const LlamaLoad &load) {
  _n_mtp_layers = llama_model_n_layer_nextn(_model);
  if (load.mtp_enabled && _n_mtp_layers > 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "HALI: MTP detected: %d nextn layers", _n_mtp_layers);
    }
    _spec_params.speculative.types = {COMMON_SPECULATIVE_TYPE_DRAFT_MTP};
    _spec_params.speculative.draft.n_max = load.mtp_n_max;
    _spec_params.speculative.draft.n_min = load.mtp_n_min;
    _spec_params.speculative.draft.p_min = load.mtp_p_min;
    _spec_params.speculative.draft.ctx_tgt = _ctx;
    _spec_params.n_batch  = load.n_batch;   // must be >= your largest process() batch
    _spec_params.n_ubatch = load.n_batch;
    _spec_params.n_outputs_max = _spec_params.n_outputs_max_per_seq = load.mtp_n_max + 1;
    _spec_params.speculative.draft.backend_sampling = true;  // draft top-k on GPU
    _spec_init = common_speculative_init_from_params(_spec_params, _model, _ctx);
    if (_spec_init) {
      _spec_params.speculative.draft.ctx_dft = _spec_init->context();
      _spec.reset(common_speculative_init(_spec_params.speculative, 1));
      _mtp_enabled = true;
      _mtp_n_max = load.mtp_n_max;
      _mtp_n_min = load.mtp_n_min;
      _mtp_p_min = load.mtp_p_min;
      // pre-allocate per-round buffers (sized to mtp_n_max + 1)
      const int vsize = load.mtp_n_max + 1;
      _mtp_acc_tokens.resize(vsize);
      _verify_toks.resize(vsize);
      _verify_pos.resize(vsize);
      _verify_n_seq.resize(vsize, 1);
      _verify_seq_ids.resize(vsize, 0);
      _verify_seq_ptrs.resize(vsize);
      for (int i = 0; i < vsize; ++i) {
        _verify_seq_ptrs[i] = &_verify_seq_ids[i];
      }
      _verify_logits.resize(vsize, 1);
      _draft_toks.resize(vsize);
      _spec_batch = common_batch(_ctx);
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write(LEVEL_DEBUG, "HALI: MTP ready: n_max=%d n_min=%d p_min=%.2f", _mtp_n_max, _mtp_n_min, _mtp_p_min);
      }
    } else {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write(LEVEL_DEBUG, "HALI: MTP speculative init failed, continuing without speculative decoding");
      }
      _n_mtp_layers = 0;
    }
  } else if (_n_mtp_layers > 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "HALI: MTP available (%d layers) but disabled", _n_mtp_layers);
    }
    _n_mtp_layers = 0;
  }
}

// ---------------------------------------------------------------------------
// Feed a decoded batch to the speculative context (replaces sync_and_capture_mtp)
// ---------------------------------------------------------------------------
void Llama::sync_and_capture_mtp(const llama_token *tokens, int n_tokens, llama_pos pos_start) {
  if (!_mtp_enabled || !_spec || n_tokens <= 0) {
    return;
  }
  mtp_trim_draft(pos_start);
  _spec_batch.clear();
  for (int i = 0; i < n_tokens; ++i) {
    _spec_batch.add(tokens[i], pos_start + i, 0, true);
  }
  if (!common_speculative_process(_spec.get(), _spec_batch)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write(LEVEL_DEBUG, "HALI: MTP: speculative process failed: %s", _last_error.c_str());
      auto * ctx_dft = _spec_init ? _spec_init->context() : nullptr;
      if (ctx_dft) {
        const llama_pos dft_pos_min = llama_memory_seq_pos_min(llama_get_memory(ctx_dft), 0);
        const llama_pos dft_pos_max = llama_memory_seq_pos_max(llama_get_memory(ctx_dft), 0);
        log_write(LEVEL_DEBUG, "HALI: MTP: ctx_dft pos range [%d, %d], batch pos [%d, %d]",
                  (int)dft_pos_min, (int)dft_pos_max, (int)pos_start, (int)(pos_start + n_tokens - 1));
      }
    }
    _last_error.clear();
    _mtp_enabled = false;
    _mtp_disabled_permanently = true;
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
// Roll back target and draft KV entries for tokens that were committed by
// mtp_round but never emitted to the caller (early termination).
// n_unemitted = number of trailing KV positions to remove.
// ---------------------------------------------------------------------------
void Llama::mtp_rollback_unemitted(int n_unemitted) {
  if (n_unemitted <= 0) {
    return;
  }
  const llama_pos keep_end = (llama_pos)(_tokens_physically_used - n_unemitted);
  llama_memory_seq_rm(llama_get_memory(_ctx), 0, keep_end, -1);
  mtp_trim_draft(keep_end);
  _tokens_physically_used -= n_unemitted;
  log_write(LEVEL_DEBUG, "HALI: MTP: rolled back %d unemitted KV tokens (keep_end=%d)",
            n_unemitted, (int)keep_end);
}

// ---------------------------------------------------------------------------
// One speculative round: draft + verify + accept
// ---------------------------------------------------------------------------
bool Llama::mtp_round(llama_token anchor, vector<llama_token> &accepted, llama_token &next) {
  accepted.clear();
  const llama_pos P = (llama_pos)_tokens_physically_used;
  const int room = _n_ctx - (int)P - 1;
  int limit = _mtp_n_max;
  if (_mtp_win_filled >= 16 && _mtp_win_drafted > 0) {
    const double rate = (double)_mtp_win_accepted / (double)_mtp_win_drafted;
    if (rate < 0.25) {
      limit = 1;
    } else if (rate < 0.50) {
      limit = 2;
    }
  }
  limit = std::min({limit, _n_batch - 1, room});
  if (limit != _mtp_last_limit && log_is_enabled(LEVEL_DEBUG)) {
    log_write(LEVEL_DEBUG, "HALI: MTP: adaptive limit %d -> %d (win_rate=%.2f)",
              _mtp_last_limit, limit,
              _mtp_win_drafted > 0 ? (double)_mtp_win_accepted / (double)_mtp_win_drafted : 0.0);
  }
  _mtp_last_limit = limit;
  if (limit <= 0) {
    return false;
  }

  // ---- 1. generate draft tokens -------------------------------------------
  auto t0 = std::chrono::high_resolution_clock::now();
  _draft_toks.clear();
  {
    auto &dp = common_speculative_get_draft_params(_spec.get(), 0);
    dp.drafting = true;
    dp.n_max = limit;
    dp.pos0 = P;
    dp.id_last = anchor;
    dp.result = &_draft_toks;
    common_speculative_draft(_spec.get());
  }

  const int k = (int)_draft_toks.size();
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
    _verify_toks[0] = anchor;
    _verify_pos[0]  = P;
    for (int i = 0; i < k; ++i) {
      _verify_toks[i + 1] = _draft_toks[i];
      _verify_pos[i + 1]  = P + 1 + i;
    }

    llama_batch batch = {
      n,
      _verify_toks.data(),
      nullptr,
      _verify_pos.data(),
      _verify_n_seq.data(),
      _verify_seq_ptrs.data(),
      _verify_logits.data()
    };

    const int rc = llama_decode(_ctx, batch);
    if (rc != 0) {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write(LEVEL_DEBUG, "HALI: MTP: verify decode failed rc=%d (k=%d pos=%d)", rc, k, (int)P);
      }
      llama_memory_seq_rm(llama_get_memory(_ctx), 0, P, -1);
      mtp_trim_draft(P);
      _mtp_enabled = false;
      _mtp_disabled_permanently = true;
      return false;
    }
  }
  auto t1 = std::chrono::high_resolution_clock::now();

  // ---- 3. feed the verify batch to the speculative context ----------------
  // drafting wrote speculative entries at P.. into ctx_dft: remove them first,
  // otherwise process() fails with "inconsistent sequence positions"
  // (process() only trims internally when chain_heads=true, i.e. n_mtp_layers>1)
  {
    mtp_trim_draft(P);
    _spec_batch.clear();
    const int n = 1 + k;
    for (int i = 0; i < n; ++i) {
      _spec_batch.add(_verify_toks[i], _verify_pos[i], 0, true);
    }
    if (!common_speculative_process(_spec.get(), _spec_batch)) {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write(LEVEL_DEBUG, "HALI: MTP: speculative process (verify) failed: %s, disabling MTP", _last_error.c_str());
      }
      _last_error.clear();
      // the draft head is now out of sync with the target: stop using it.
      // this round's verify results are still valid, so finish it below.
      _mtp_enabled = false;
      _mtp_disabled_permanently = true;
    }
  }

  // ---- 4. sample and accept ------------------------------------------------
  // common_sampler_sample_and_accept_n returns at least 1 token, up to k+1.
  // ids[0..n_acc-1] are the accepted draft tokens, ids.back() is the next token
  // (either the mismatch correction or the bonus token after a full accept).
  auto ids = common_sampler_sample_and_accept_n(_smpl_mtp, _ctx, _draft_toks);

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
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write(LEVEL_DEBUG, "HALI: MTP: seq_rm failed, disabling MTP");
      }
      _mtp_enabled = false;
      _mtp_disabled_permanently = true;
      return false;
    }
  }

  _tokens_physically_used += n_acc + 1;

  // ---- timing: gpu = draft+verify, cpu = process+sample+rollback -----------
  auto t_end = std::chrono::high_resolution_clock::now();
  _mtp_time_gpu += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  _mtp_time_cpu += std::chrono::duration_cast<std::chrono::microseconds>(t_end - t1).count();

  ++_mtp_rounds;
  _mtp_drafted += k;
  _mtp_accepted += n_acc;
  // update sliding window
  if (_mtp_win_filled == MTP_WIN) {
    _mtp_win_drafted  -= _mtp_win[_mtp_win_idx].drafted;
    _mtp_win_accepted -= _mtp_win[_mtp_win_idx].accepted;
  }
  _mtp_win[_mtp_win_idx] = {k, n_acc};
  _mtp_win_drafted  += k;
  _mtp_win_accepted += n_acc;
  _mtp_win_idx = (_mtp_win_idx + 1) % MTP_WIN;
  if (_mtp_win_filled < MTP_WIN) { _mtp_win_filled++; }
  if (log_is_enabled(LEVEL_DEBUG)) {
    const long gpu_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const long cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t1).count();
    log_write(LEVEL_DEBUG, "HALI: MTP: %d/%d accepted (limit=%d) gpu=%ld cpu=%ld",
              n_acc, k, limit, gpu_us, cpu_us);
  }
  if (_mtp_rounds % 128 == 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      const double r = (double)_mtp_rounds;
      log_write(LEVEL_DEBUG,
                "HALI: MTP: %llu rounds, %.2f tok/rnd, accept %.0f%% | "
                "gpu %.1f cpu %.1f (us)",
                (unsigned long long)_mtp_rounds,
                1.0 + (double)_mtp_accepted / r,
                _mtp_drafted ? 100.0 * (double)_mtp_accepted / (double)_mtp_drafted : 0.0,
                (double)_mtp_time_gpu / r,
                (double)_mtp_time_cpu / r);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// wipe everything MTP related (after a context flush / reset).
// Callers clear the target KV first, this empties the draft context to match.
// ---------------------------------------------------------------------------
void Llama::mtp_reset_state() {
  mtp_buf_clear();
  _has_pending = false;
  _anchor = LLAMA_TOKEN_NULL;
  for (auto &e : _mtp_win) { e = {0, 0}; }
  _mtp_win_idx      = 0;
  _mtp_win_filled   = 0;
  _mtp_win_drafted  = 0;
  _mtp_win_accepted = 0;
  _mtp_last_limit   = 0;

  auto *ctx_dft = _spec_init ? _spec_init->context() : nullptr;
  if (!_spec || !ctx_dft) {
    return;  // MTP not set up (e.g. the call from the constructor)
  }

  // reuse the speculative object and draft context, only empty the draft KV
  llama_memory_clear(llama_get_memory(ctx_dft), true);

  // re-enable MTP after a context reset, unless it was disabled due to a
  // permanent failure (e.g. speculative process or verify decode error)
  if (!_mtp_disabled_permanently) {
    _mtp_enabled = true;
  }
}

bool Llama::configure_mtp_sampler() {
  if (_mtp_enabled) {
    if (_smpl_mtp) {
      common_sampler_free(_smpl_mtp);
      _smpl_mtp = nullptr;
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
    if (!_grammar_src.empty()) {
      sp.grammar = common_grammar(COMMON_GRAMMAR_TYPE_USER, _grammar_src, _grammar_root);
    }
    _smpl_mtp = common_sampler_init(_model, sp);
    if (!_smpl_mtp) {
      set_last_error("failed to initialize MTP sampler");
      return false;
    }
  }
  return true;
}

bool Llama::decode_anchor(llama_token tok) {
  auto t0 = std::chrono::high_resolution_clock::now();
  llama_batch batch = llama_batch_get_one(&tok, 1);
  if (llama_decode(_ctx, batch)) {
    return false;
  }
  _tokens_physically_used += 1;
  sync_and_capture_mtp(&tok, 1, (llama_pos)(_tokens_physically_used - 1));
  _mtp_time_cpu += std::chrono::duration_cast<std::chrono::microseconds>(
                                                                            std::chrono::high_resolution_clock::now() - t0).count();
  return true;
}

string Llama::emit_token(LlamaIter &iter, llama_token tok) {
  if (llama_vocab_is_eog(_vocab, tok)) {
    iter._has_next = false;
    // Roll back any accepted drafts still in KV but never emitted to the caller
    int n_unemitted = mtp_buf_empty() ? 0 : mtp_buf_size() - 1;
    mtp_rollback_unemitted(n_unemitted);
    mtp_buf_clear();
    _has_pending = false;
    return "";
  }
  string result = token_to_string(iter, tok);
  if (!iter._has_next) {
    // Early termination (stop word / max tokens).
    // The last element in the buffer is always the anchor (not yet in KV);
    // all others are accepted drafts that ARE in KV and must be rolled back.
    const bool is_anchor = _has_pending && mtp_buf_empty() && tok == _anchor;
    int n_unemitted = mtp_buf_empty() ? 0 : mtp_buf_size() - 1;
    mtp_rollback_unemitted(n_unemitted);
    mtp_buf_clear();
    _has_pending = false;
    if (is_anchor) {
      // The anchor was never decoded into KV; do it now so the
      // conversation continues from a consistent state.
      decode_anchor(tok);
    }
  }
  return result;
}

