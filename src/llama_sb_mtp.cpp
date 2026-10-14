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
  _mtp.n_layers = llama_model_n_layer_nextn(_model);
  if (load.mtp_enabled && _mtp.n_layers > 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP detected: %d nextn layers", _mtp.n_layers);
    }
    _mtp.spec_params.speculative.types = {COMMON_SPECULATIVE_TYPE_DRAFT_MTP};
    _mtp.spec_params.speculative.draft.n_max = load.mtp_n_max;
    _mtp.spec_params.speculative.draft.n_min = load.mtp_n_min;
    _mtp.spec_params.speculative.draft.p_min = load.mtp_p_min;
    _mtp.spec_params.speculative.draft.ctx_tgt = _ctx;
    _mtp.spec_params.n_batch  = load.n_batch;   // must be >= your largest process() batch
    _mtp.spec_params.n_ubatch = load.n_batch;
    _mtp.spec_params.n_outputs_max = _mtp.spec_params.n_outputs_max_per_seq = load.mtp_n_max + 1;
    _mtp.spec_params.speculative.draft.backend_sampling = true;  // draft top-k on GPU
    _mtp.spec_init = common_speculative_init_from_params(_mtp.spec_params, _model, _ctx);
    if (_mtp.spec_init) {
      _mtp.spec_params.speculative.draft.ctx_dft = _mtp.spec_init->context();
      _mtp.spec.reset(common_speculative_init(_mtp.spec_params.speculative, 1));
      _mtp.enabled = true;
      _mtp.n_max = load.mtp_n_max;
      _mtp.n_min = load.mtp_n_min;
      _mtp.p_min = load.mtp_p_min;
      _mtp.adaptive_enabled = load.mtp_adaptive;
      _mtp.adapt.eff_n_max = load.mtp_n_max;
      _mtp.adapt.p_min = load.mtp_p_min;
      _mtp.adapt.n_min = load.mtp_n_min > 0 ? load.mtp_n_min : 1;
      // pre-allocate per-round buffers (sized to mtp_n_max + 1)
      const int vsize = load.mtp_n_max + 1;
      _mtp.acc_tokens.resize(vsize);
      _mtp.verify_toks.resize(vsize);
      _mtp.verify_pos.resize(vsize);
      _mtp.verify_n_seq.resize(vsize, 1);
      _mtp.verify_seq_ids.resize(vsize, 0);
      _mtp.verify_seq_ptrs.resize(vsize);
      for (int i = 0; i < vsize; ++i) {
        _mtp.verify_seq_ptrs[i] = &_mtp.verify_seq_ids[i];
      }
      _mtp.verify_logits.resize(vsize, 1);
      _mtp.draft_toks.resize(vsize);
      _mtp.spec_batch = common_batch(_ctx);
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP ready: n_max=%d n_min=%d p_min=%.2f", _mtp.n_max, _mtp.n_min, _mtp.p_min);
      }
    } else {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP speculative init failed, continuing without speculative decoding");
      }
      _mtp.n_layers = 0;
    }
  } else if (_mtp.n_layers > 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP available (%d layers) but disabled", _mtp.n_layers);
    }
    _mtp.n_layers = 0;
  }
}

// ---------------------------------------------------------------------------
// Feed a decoded batch to the speculative context (replaces sync_and_capture_mtp)
// ---------------------------------------------------------------------------
void Llama::sync_and_capture_mtp(const llama_token *tokens, int n_tokens, llama_pos pos_start) {
  if (!_mtp.enabled || !_mtp.spec || n_tokens <= 0) {
    return;
  }
  mtp_trim_draft(pos_start);
  _mtp.spec_batch.clear();
  for (int i = 0; i < n_tokens; ++i) {
    _mtp.spec_batch.add(tokens[i], pos_start + i, 0, true);
  }
  if (!common_speculative_process(_mtp.spec.get(), _mtp.spec_batch)) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: speculative process failed: %s", _last_error.c_str());
      auto * ctx_dft = _mtp.spec_init ? _mtp.spec_init->context() : nullptr;
      if (ctx_dft) {
        const llama_pos dft_pos_min = llama_memory_seq_pos_min(llama_get_memory(ctx_dft), 0);
        const llama_pos dft_pos_max = llama_memory_seq_pos_max(llama_get_memory(ctx_dft), 0);
        log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: ctx_dft pos range [%d, %d], batch pos [%d, %d]",
                  (int)dft_pos_min, (int)dft_pos_max, (int)pos_start, (int)(pos_start + n_tokens - 1));
      }
    }
    _last_error.clear();
    _mtp.enabled = false;
    _mtp.disabled_permanently = true;
  }
}

// ---------------------------------------------------------------------------
// Drop draft-context KV entries at positions >= from, so process()/draft()
// can rewrite them (llama requires strictly increasing positions per sequence)
// ---------------------------------------------------------------------------
void Llama::mtp_trim_draft(llama_pos from) {
  auto *ctx_dft = _mtp.spec_init ? _mtp.spec_init->context() : nullptr;
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
  log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: rolled back %d unemitted KV tokens (keep_end=%d)",
            n_unemitted, (int)keep_end);
}

// ---------------------------------------------------------------------------
// One speculative round: draft + verify + accept
// ---------------------------------------------------------------------------
bool Llama::mtp_round(llama_token anchor, vector<llama_token> &accepted, llama_token &next) {
  accepted.clear();
  const llama_pos P = (llama_pos)_tokens_physically_used;
  const int room = _mtp.n_ctx - (int)P - 1;
  int limit = _mtp.adapt.eff_n_max;
  limit = std::min({limit, _mtp.n_batch - 1, room});
  if (limit != _mtp.last_limit && log_is_enabled(LEVEL_DEBUG)) {
    log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: adaptive limit %d -> %d (eff_n_max=%d)",
              _mtp.last_limit, limit, _mtp.adapt.eff_n_max);
  }
  _mtp.last_limit = limit;
  if (limit <= 0) {
    return false;
  }

  // ---- 1. generate draft tokens -------------------------------------------
  auto t0 = std::chrono::high_resolution_clock::now();
  _mtp.draft_toks.clear();
  {
    auto &dp = common_speculative_get_draft_params(_mtp.spec.get(), 0);
    dp.drafting = true;
    dp.n_max = limit;
    // NOTE: p_min is baked into the speculative object at init time.
    // _mtp.adapt.p_min tracks the ideal value; wiring it to the API
    // requires per-round p_min support in common_speculative_draft_params.
    dp.pos0 = P;
    dp.id_last = anchor;
    dp.result = &_mtp.draft_toks;
    common_speculative_draft(_mtp.spec.get());
  }

  const int k = (int)_mtp.draft_toks.size();
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
    _mtp.verify_toks[0] = anchor;
    _mtp.verify_pos[0]  = P;
    for (int i = 0; i < k; ++i) {
      _mtp.verify_toks[i + 1] = _mtp.draft_toks[i];
      _mtp.verify_pos[i + 1]  = P + 1 + i;
    }

    llama_batch batch = {
      n,
      _mtp.verify_toks.data(),
      nullptr,
      _mtp.verify_pos.data(),
      _mtp.verify_n_seq.data(),
      _mtp.verify_seq_ptrs.data(),
      _mtp.verify_logits.data()
    };

    const int rc = llama_decode(_ctx, batch);
    if (rc != 0) {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: verify decode failed rc=%d (k=%d pos=%d)", rc, k, (int)P);
      }
      llama_memory_seq_rm(llama_get_memory(_ctx), 0, P, -1);
      mtp_trim_draft(P);
      _mtp.enabled = false;
      _mtp.disabled_permanently = true;
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
    _mtp.spec_batch.clear();
    const int n = 1 + k;
    for (int i = 0; i < n; ++i) {
      _mtp.spec_batch.add(_mtp.verify_toks[i], _mtp.verify_pos[i], 0, true);
    }
    if (!common_speculative_process(_mtp.spec.get(), _mtp.spec_batch)) {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: speculative process (verify) failed: %s, disabling MTP", _last_error.c_str());
      }
      _last_error.clear();
      // the draft head is now out of sync with the target: stop using it.
      // this round's verify results are still valid, so finish it below.
      _mtp.enabled = false;
      _mtp.disabled_permanently = true;
    }
  }

  // ---- 4. sample and accept ------------------------------------------------
  // common_sampler_sample_and_accept_n returns at least 1 token, up to k+1.
  // ids[0..n_acc-1] are the accepted draft tokens, ids.back() is the next token
  // (either the mismatch correction or the bonus token after a full accept).
  auto ids = common_sampler_sample_and_accept_n(_mtp.smpl, _ctx, _mtp.draft_toks);

  const int n_acc = (int)ids.size() - 1;  // number of draft tokens accepted
  next = ids.back();

  for (int i = 0; i < n_acc; ++i) {
    accepted.push_back(ids[i]);
  }

  // ---- 5. inform the speculative context of the acceptance ----------------
  common_speculative_accept(_mtp.spec.get(), 0, (uint16_t)n_acc);

  // ---- 6. rollback rejected positions --------------------------------------
  if (n_acc < k) {
    const llama_pos keep_end = P + n_acc + 1;  // [P, keep_end) stays committed
    mtp_trim_draft(keep_end);  // drop rejected rows from the draft cache too
    if (!llama_memory_seq_rm(llama_get_memory(_ctx), 0, keep_end, -1)) {
      if (log_is_enabled(LEVEL_DEBUG)) {
        log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: seq_rm failed, disabling MTP");
      }
      _mtp.enabled = false;
      _mtp.disabled_permanently = true;
      return false;
    }
  }

  _tokens_physically_used += n_acc + 1;

  // ---- timing: gpu = draft+verify, cpu = process+sample+rollback -----------
  auto t_end = std::chrono::high_resolution_clock::now();
  _mtp.time_gpu += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  _mtp.time_cpu += std::chrono::duration_cast<std::chrono::microseconds>(t_end - t1).count();

  ++_mtp.rounds;
  _mtp.drafted += k;
  _mtp.accepted += n_acc;
  // update sliding window
  if (_mtp.win_filled == MtpState::WIN) {
    _mtp.win_drafted  -= _mtp.win[_mtp.win_idx].drafted;
    _mtp.win_accepted -= _mtp.win[_mtp.win_idx].accepted;
  }
  {
    auto &w = _mtp.win[_mtp.win_idx];
    w.drafted  = k;
    w.accepted = n_acc;
    for (int i = 0; i < 4; ++i) {
      w.pos_acc[i] = (i < n_acc) ? 1 : 0;
    }
  }
  _mtp.win_drafted  += k;
  _mtp.win_accepted += n_acc;
  _mtp.win_idx = (_mtp.win_idx + 1) % MtpState::WIN;
  if (_mtp.win_filled < MtpState::WIN) { _mtp.win_filled++; }
  if (log_is_enabled(LEVEL_DEBUG)) {
    const long gpu_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const long cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t1).count();
    log_write_cat(LEVEL_DEBUG, "mtp", "HALI: MTP: %d/%d accepted (limit=%d) gpu=%ld cpu=%ld",
              n_acc, k, limit, gpu_us, cpu_us);
  }
  if (_mtp.rounds % 128 == 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      const double r = (double)_mtp.rounds;
      log_write_cat(LEVEL_DEBUG, "mtp",
                "HALI: MTP: %llu rounds, %.2f tok/rnd, accept %.0f%% | "
                "gpu %.1f cpu %.1f (us) | n_max=%d p_min=%.2f n_min=%d",
                (unsigned long long)_mtp.rounds,
                1.0 + (double)_mtp.accepted / r,
                _mtp.drafted ? 100.0 * (double)_mtp.accepted / (double)_mtp.drafted : 0.0,
                (double)_mtp.time_gpu / r,
                (double)_mtp.time_cpu / r,
                _mtp.adapt.eff_n_max,
                (double)_mtp.adapt.p_min,
                _mtp.adapt.n_min);
    }
  }
  if (++_mtp.adapt.eval_counter >= 32) {
    mtp_adapt_eval();
  }
  return true;
}

// ---------------------------------------------------------------------------
// Compute the effective n_max from per-position acceptance rates.
// Returns the largest k such that pos_rate[0..k-1] are all above pos_threshold.
// ---------------------------------------------------------------------------
int Llama::compute_effective_n_max() {
  if (_mtp.win_filled < 16) {
    return _mtp.n_max;  // not enough data, use config ceiling
  }
  int k = 0;
  for (int i = 0; i < _mtp.n_max; ++i) {
    int sum = 0;
    for (int j = 0; j < _mtp.win_filled; ++j) {
      sum += _mtp.win[j].pos_acc[i];
    }
    double rate = (double)sum / (double)_mtp.win_filled;
    // Hysteresis: extending beyond current eff_n_max requires a higher bar
    double threshold = (i >= _mtp.adapt.eff_n_max)
      ? _mtp.adapt.pos_threshold + 0.05
      : _mtp.adapt.pos_threshold;
    if (rate < threshold) break;
    k = i + 1;
  }
  return std::max(k, 1);  // always draft at least 1
}

// ---------------------------------------------------------------------------
// Adapt p_min based on the window acceptance fraction.
// High fraction → MTP head is too conservative → lower p_min.
// Low fraction  → MTP head is drafting low-quality tokens → raise p_min.
// ---------------------------------------------------------------------------
void Llama::adapt_p_min() {
  if (_mtp.win_drafted == 0) return;
  double frac = (double)_mtp.win_accepted / (double)_mtp.win_drafted;
  if (frac > 0.80 && _mtp.adapt.p_min > 0.0f) {
    float old = _mtp.adapt.p_min;
    _mtp.adapt.p_min = std::max(0.0f, _mtp.adapt.p_min - 0.05f);
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive p_min %.2f -> %.2f (win_frac=%.2f)",
        old, _mtp.adapt.p_min, frac);
    }
  } else if (frac < 0.30 && _mtp.adapt.p_min < 0.50f) {
    float old = _mtp.adapt.p_min;
    _mtp.adapt.p_min = std::min(0.50f, _mtp.adapt.p_min + 0.05f);
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive p_min %.2f -> %.2f (win_frac=%.2f)",
        old, _mtp.adapt.p_min, frac);
    }
  }
}

// ---------------------------------------------------------------------------
// Circuit breaker: if position 0 acceptance is consistently very low,
// allow zero drafts (n_min=0). Restore when it recovers.
// ---------------------------------------------------------------------------
void Llama::adapt_n_min() {
  if (_mtp.win_filled < MtpState::WIN) return;
  int sum = 0;
  for (int j = 0; j < _mtp.win_filled; ++j) {
    sum += _mtp.win[j].pos_acc[0];
  }
  double rate0 = (double)sum / (double)_mtp.win_filled;
  if (rate0 < 0.15 && _mtp.adapt.n_min > 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive n_min %d -> 0 (pos_rate[0]=%.2f < 0.15)",
        _mtp.adapt.n_min, rate0);
    }
    _mtp.adapt.n_min = 0;
  } else if (rate0 > 0.40 && _mtp.adapt.n_min == 0) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive n_min 0 -> 1 (pos_rate[0]=%.2f > 0.40)",
        rate0);
    }
    _mtp.adapt.n_min = 1;
  }
}

// ---------------------------------------------------------------------------
// Meta-adaptation: tune the pos_threshold itself based on long-term
// acceptance rate. High overall rate → be more aggressive (raise threshold).
// Low overall rate → be more conservative (lower threshold).
// ---------------------------------------------------------------------------
void Llama::adapt_pos_threshold() {
  if (_mtp.drafted == 0) return;
  double ema = (double)_mtp.accepted / (double)_mtp.drafted;
  if (ema > 0.70 && _mtp.adapt.pos_threshold < 0.40f) {
    float old = _mtp.adapt.pos_threshold;
    _mtp.adapt.pos_threshold = std::min(0.50f, _mtp.adapt.pos_threshold + 0.05f);
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive pos_threshold %.2f -> %.2f (ema=%.2f)",
        old, _mtp.adapt.pos_threshold, ema);
    }
  } else if (ema < 0.30 && _mtp.adapt.pos_threshold > 0.20f) {
    float old = _mtp.adapt.pos_threshold;
    _mtp.adapt.pos_threshold = std::max(0.15f, _mtp.adapt.pos_threshold - 0.05f);
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive pos_threshold %.2f -> %.2f (ema=%.2f)",
        old, _mtp.adapt.pos_threshold, ema);
    }
  }
}

// ---------------------------------------------------------------------------
// Adaptive parameter evaluation — called every 32 rounds (one full window).
// Adjusts eff_n_max, p_min, n_min, and pos_threshold based on accumulated
// acceptance experience.
// ---------------------------------------------------------------------------
void Llama::mtp_adapt_eval() {
  if (!_mtp.adaptive_enabled) {
    _mtp.adapt.eval_counter = 0;
    return;
  }
  if (_mtp.adapt.cooldown > 0) {
    _mtp.adapt.cooldown--;
    _mtp.adapt.eval_counter = 0;
    return;
  }
  if (_mtp.win_filled < MtpState::WIN) {
    _mtp.adapt.eval_counter = 0;
    return;
  }

  // 1. per-position n_max
  int new_nmax = compute_effective_n_max();
  if (new_nmax != _mtp.adapt.eff_n_max) {
    if (log_is_enabled(LEVEL_DEBUG)) {
      log_write_cat(LEVEL_DEBUG, "mtp",
        "HALI: MTP: adaptive n_max %d -> %d",
        _mtp.adapt.eff_n_max, new_nmax);
    }
    _mtp.adapt.eff_n_max = new_nmax;
    _mtp.adapt.cooldown = 32;
  }

  // 2. p_min
  adapt_p_min();

  // 3. n_min (circuit breaker)
  adapt_n_min();

  // 4. meta: pos_threshold
  adapt_pos_threshold();

  _mtp.adapt.eval_counter = 0;
}

// ---------------------------------------------------------------------------
// wipe everything MTP related (after a context flush / reset).
// Callers clear the target KV first, this empties the draft context to match.
// ---------------------------------------------------------------------------
void Llama::mtp_reset_state() {
  _mtp.buf_clear();
  _mtp.has_pending = false;
  _mtp.anchor = LLAMA_TOKEN_NULL;
  for (auto &e : _mtp.win) { e = {0, 0, {}}; }
  _mtp.win_idx      = 0;
  _mtp.win_filled   = 0;
  _mtp.win_drafted  = 0;
  _mtp.win_accepted = 0;
  _mtp.last_limit   = 0;
  _mtp.adapt.eval_counter = 0;
  _mtp.adapt.cooldown     = 0;

  auto *ctx_dft = _mtp.spec_init ? _mtp.spec_init->context() : nullptr;
  if (!_mtp.spec || !ctx_dft) {
    return;  // MTP not set up (e.g. the call from the constructor)
  }

  // reuse the speculative object and draft context, only empty the draft KV
  llama_memory_clear(llama_get_memory(ctx_dft), true);

  // re-enable MTP after a context reset, unless it was disabled due to a
  // permanent failure (e.g. speculative process or verify decode error)
  if (!_mtp.disabled_permanently) {
    _mtp.enabled = true;
  }
}

bool Llama::configure_mtp_sampler() {
  if (_mtp.enabled) {
    if (_mtp.smpl) {
      common_sampler_free(_mtp.smpl);
      _mtp.smpl = nullptr;
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
    _mtp.smpl = common_sampler_init(_model, sp);
    if (!_mtp.smpl) {
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
  _mtp.time_cpu += std::chrono::duration_cast<std::chrono::microseconds>(
                                                                            std::chrono::high_resolution_clock::now() - t0).count();
  return true;
}

string Llama::emit_token(LlamaIter &iter, llama_token tok) {
  if (llama_vocab_is_eog(_vocab, tok)) {
    iter._has_next = false;
    // Roll back any accepted drafts still in KV but never emitted to the caller
    int n_unemitted = _mtp.buf_empty() ? 0 : _mtp.buf_size() - 1;
    mtp_rollback_unemitted(n_unemitted);
    _mtp.buf_clear();
    _mtp.has_pending = false;
    return "";
  }
  string result = token_to_string(iter, tok);
  if (!iter._has_next) {
    // Early termination (stop word / max tokens).
    // The last element in the buffer is always the anchor (not yet in KV);
    // all others are accepted drafts that ARE in KV and must be rolled back.
    const bool is_anchor = _mtp.has_pending && _mtp.buf_empty() && tok == _mtp.anchor;
    int n_unemitted = _mtp.buf_empty() ? 0 : _mtp.buf_size() - 1;
    mtp_rollback_unemitted(n_unemitted);
    _mtp.buf_clear();
    _mtp.has_pending = false;
    if (is_anchor) {
      // The anchor was never decoded into KV; do it now so the
      // conversation continues from a consistent state.
      decode_anchor(tok);
    }
  }
  return result;
}

