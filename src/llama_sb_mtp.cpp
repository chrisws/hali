// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cstring>
#include <vector>

#include "llama.h"
#include "llama_sb.h"
#include "llama-ext.h"
#include "logging.h"

void Llama::sync_and_capture_mtp(const llama_token * tokens, int n_tokens, llama_pos pos_start) {
  if (!_mtp_enabled || n_tokens <= 0) {
    return;
  }

  const float *h_tgt = llama_get_embeddings_nextn(_ctx);
  if (!h_tgt) {
    return;
  }

  const size_t row_bytes = (size_t) _n_embd * sizeof(float);

  // save the old pending state before we overwrite it
  vector<float> old_pending_h = _pending_h;
  bool has_prior_state = !old_pending_h.empty();
  // build the MTP batch with the same tokens
  llama_batch_ext_clear(_batch_mtp);
  for (int k = 0; k < n_tokens; ++k) {
    const int32_t idx = llama_batch_ext_add_token(_batch_mtp, 0, tokens[k]);
    if (idx < 0) {
      return;
    }
    llama_pos pos = pos_start + k;
    llama_batch_ext_set_pos(_batch_mtp, idx, &pos);
    // inject hidden state only when we have a carryover from a prior step.
    // on the first call (prompt processing) there is no prior state, so
    // the MTP context uses normal token embeddings to build its KV cache.
    if (has_prior_state) {
      const float * h_row = (k == 0) ? old_pending_h.data() : h_tgt + (size_t)(k - 1) * _n_embd;
      llama_batch_ext_set_embd_state(_batch_mtp, idx, {h_row, 1, (size_t)_n_embd});
    }
  }

  // process on the MTP context (no output needed)
  llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, _batch_mtp);

  // capture hidden states from the target context
  // h_tgt has one row per token in the last batch (masked=false),
  // so it contains exactly n_tokens * n_embd floats.
  _verify_h.resize((size_t) n_tokens * _n_embd);
  std::memcpy(_verify_h.data(), h_tgt, (size_t) n_tokens * row_bytes);
  _verify_h_rows = n_tokens;

  // the last row is the pending state for the next draft step
  _pending_h.resize(_n_embd);
  std::memcpy(_pending_h.data(), h_tgt + (size_t)(n_tokens - 1) * _n_embd, row_bytes);
}

vector<llama_token> Llama::generate_draft_tokens(llama_token last_token, llama_pos pos) {
  vector<llama_token> drafts;
  if (!_mtp_enabled || _pending_h.empty()) {
    return drafts;
  }

  vector<float> h_state = _pending_h;

  for (int i = 0; i < _mtp_n_max; ++i) {
    // build a single-token batch
    llama_batch_ext_clear(_batch_mtp);
    const int32_t idx = llama_batch_ext_add_token(_batch_mtp, 0, last_token);
    if (idx < 0) {
      break;
    }
    llama_batch_ext_set_pos(_batch_mtp, idx, &pos);
    llama_batch_ext_set_embd_state(_batch_mtp, idx, {h_state.data(), 1, (size_t)_n_embd});
    llama_batch_ext_set_output_logits(_batch_mtp, idx, true);

    // process on the MTP context
    int ret = llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, _batch_mtp);
    if (ret != 0) {
      break;
    }

    // sample the next token
    llama_token tok = llama_sampler_sample(_sampler_mtp, _ctx_mtp, -1);
    if (llama_vocab_is_eog(_vocab, tok)) {
      break;
    }

    drafts.push_back(tok);

    // update hidden state from the MTP context
    const float * h_row = llama_get_embeddings_nextn_ith(_ctx_mtp, 0);
    if (!h_row) {
      break;
    }
    std::memcpy(h_state.data(), h_row, (size_t)_n_embd * sizeof(float));

    // advance
    last_token = tok;
    ++pos;
  }

  return drafts;
}

vector<llama_token> Llama::verify_and_accept(vector<llama_token> &drafts) {
  vector<llama_token> accepted;
  if (drafts.empty() || !_mtp_enabled) {
    return accepted;
  }

  int k = (int)drafts.size();
  int n_vocab = llama_vocab_n_tokens(_vocab);
  llama_pos pos = (llama_pos)_tokens_physically_used;

  // save target logits at current position (predicts the first draft token)
  const float *logits_cur = llama_get_logits(_ctx);
  vector<float> saved_logits(logits_cur, logits_cur + n_vocab);

  // build verification batch with all draft tokens
  llama_batch batch = llama_batch_init(k, 0, 1);
  for (int i = 0; i < k; ++i) {
    batch.token[i] = drafts[i];
    batch.pos[i] = pos + i;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i] = 0;
    batch.logits[i] = 1;
  }

  if (llama_decode(_ctx, batch) != 0) {
    llama_batch_free(batch);
    return accepted;
  }

  // verify each draft against the target
  int n_accepted = 0;
  for (int i = 0; i < k; ++i) {
    const float *logits;
    if (i == 0) {
      logits = saved_logits.data();
    } else {
      logits = llama_get_logits_ith(_ctx, i - 1);
    }

    // argmax over vocab
    int best = 0;
    float best_val = -1e30f;
    for (int j = 0; j < n_vocab; ++j) {
      if (logits[j] > best_val) {
        best_val = logits[j];
        best = j;
      }
    }

    if (best == (int)drafts[i]) {
      accepted.push_back(drafts[i]);
      n_accepted++;
    } else {
      // mismatch: accept the target's token at this position
      accepted.push_back((llama_token)best);

      // roll back rejected drafts from target KV cache
      if (n_accepted < k) {
        llama_memory_t mem = llama_get_memory(_ctx);
        llama_memory_seq_rm(mem, 0, pos + n_accepted, -1);
      }
      // roll back MTP context
      if (_ctx_mtp) {
        llama_memory_t mtp_mem = llama_get_memory(_ctx_mtp);
        llama_memory_seq_rm(mtp_mem, 0, pos + n_accepted, -1);
      }

      // process the correction token on the target
      llama_token corr = (llama_token)best;
      llama_batch corr_batch = llama_batch_get_one(&corr, 1);
      corr_batch.pos[0] = pos + n_accepted;
      llama_decode(_ctx, corr_batch);
      _tokens_physically_used += n_accepted + 1;

      // capture hidden state from the correction decode
      const float *h = llama_get_embeddings_nextn(_ctx);

      // process the correction token on the MTP context
      if (_ctx_mtp) {
        llama_batch_ext_clear(_batch_mtp);
        int32_t idx = llama_batch_ext_add_token(_batch_mtp, 0, corr);
        if (idx >= 0) {
          llama_pos cpos = pos + n_accepted;
          llama_batch_ext_set_pos(_batch_mtp, idx, &cpos);
          if (h) {
            llama_batch_ext_set_embd_state(_batch_mtp, idx, {h, 1, (size_t)_n_embd});
          }
          llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, _batch_mtp);
        }
      }

      if (h) {
        _pending_h.resize(_n_embd);
        std::memcpy(_pending_h.data(), h, (size_t)_n_embd * sizeof(float));
      }

      log_write(LEVEL_DEBUG, "HALI: MTP: %d/%d accepted (mismatch at %d)", n_accepted, k, i);
      llama_batch_free(batch);
      return accepted;
    }
  }

  // all drafts accepted: compute bonus token from the last position
  const float *logits_last = llama_get_logits_ith(_ctx, k - 1);
  int best = 0;
  float best_val = -1e30f;
  for (int j = 0; j < n_vocab; ++j) {
    if (logits_last[j] > best_val) {
      best_val = logits_last[j];
      best = j;
    }
  }
  llama_token bonus = (llama_token)best;

  // process the bonus token on the target
  llama_batch bonus_batch = llama_batch_get_one(&bonus, 1);
  bonus_batch.pos[0] = pos + k;
  llama_decode(_ctx, bonus_batch);

  // process the bonus token on the MTP context
  if (_ctx_mtp) {
    const float *h_bonus = llama_get_embeddings_nextn(_ctx);
    llama_batch_ext_clear(_batch_mtp);
    int32_t idx = llama_batch_ext_add_token(_batch_mtp, 0, bonus);
    if (idx >= 0) {
      llama_pos bpos = pos + k;
      llama_batch_ext_set_pos(_batch_mtp, idx, &bpos);
      if (h_bonus) {
        llama_batch_ext_set_embd_state(_batch_mtp, idx, {h_bonus, 1, (size_t)_n_embd});
      }
      llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, _batch_mtp);
    }
  }

  _tokens_physically_used += k + 1;

  // capture hidden state from the bonus decode
  const float *h = llama_get_embeddings_nextn(_ctx);
  if (h) {
    _pending_h.resize(_n_embd);
    std::memcpy(_pending_h.data(), h, (size_t)_n_embd * sizeof(float));
  }

  accepted.push_back(bonus);
  log_write(LEVEL_DEBUG, "HALI: MTP: %d/%d accepted + bonus", k, k);
  llama_batch_free(batch);
  return accepted;
}