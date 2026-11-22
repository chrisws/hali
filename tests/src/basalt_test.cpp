// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// Unit tests for the Basalt engine wrapper (no GPU model required).
// Verifies interface compliance, capability gates, and default state.

#include <cassert>
#include <cstdio>
#include <cstring>
#include "basalt/basalt.h"
#include "basalt/artifact/gguf_reader.hpp"
#include "models/qwen35/meta.hpp"
#include "engine.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
  } while (0)

// --- test cases -----------------------------------------------------------

static bool test_constructor_destructor() {
  {
    Basalt b;
    (void)b;
  }
  return true;
}

static bool test_is_engine() {
  // Basalt must be usable as an Engine*
  Basalt b;
  Engine *e = &b;
  assert(e != nullptr);
  return true;
}

static bool test_supports_rag_false() {
  Basalt b;
  assert(b.supports_rag() == false);
  return true;
}

static bool test_supports_mtp_tuning_false() {
  Basalt b;
  assert(b.supports_mtp_tuning() == false);
  return true;
}

static bool test_supports_kv_control_false() {
  Basalt b;
  assert(b.supports_kv_control() == false);
  return true;
}

static bool test_last_error_empty_initially() {
  Basalt b;
  const char *err = b.last_error();
  assert(err != nullptr);
  assert(std::strlen(err) == 0);
  return true;
}

static bool test_memory_info_zeros_before_load() {
  Basalt b;
  ModelMemoryInfo mi = b.memory_info();
  assert(mi.kv_used == 0);
  assert(mi.kv_total == 0);
  assert(mi.kv_percent == 0.0f);
  assert(mi.vram_used == 0);
  assert(mi.vram_total == 0);
  assert(mi.vram_percent == 0.0f);
  assert(mi.n_layers_total == 0);
  assert(mi.n_layers_gpu == 0);
  assert(mi.n_layers_cpu == 0);
  assert(mi.model_native_max_ctx == 0);
  return true;
}

static bool test_memory_kv_percent_zero() {
  Basalt b;
  assert(b.memory_kv_percent() == 0.0f);
  return true;
}

static bool test_is_memory_flush_false() {
  Basalt b;
  assert(b.is_memory_flush() == false);
  return true;
}

static bool test_is_gemma_4_false() {
  Basalt b;
  assert(b.is_gemma_4() == false);
  return true;
}

static bool test_is_can_shift_false() {
  Basalt b;
  assert(b.is_can_shift() == false);
  return true;
}

static bool test_mtp_has_best_false() {
  Basalt b;
  assert(b.mtp_has_best() == false);
  assert(b.mtp_best_n_max() == 0);
  assert(b.mtp_best_n_min() == 0);
  assert(b.mtp_best_p_min() == 0.0f);
  assert(b.mtp_best_pos_threshold() == 0.0f);
  return true;
}

static bool test_sampling_setters_no_crash() {
  Basalt b;
  b.set_temperature(0.7f);
  b.set_top_k(40);
  b.set_top_p(0.9f);
  b.set_min_p(0.05f);
  b.set_penalty_repeat(1.1f);
  b.set_penalty_freq(0.5f);
  b.set_penalty_present(0.3f);
  b.set_penalty_last_n(64);
  b.set_max_tokens(1024);
  b.set_seed(42);
  b.set_log_level(3);
  return true;
}

static bool test_add_stop_no_crash() {
  Basalt b;
  b.add_stop("\n");
  b.add_stop("HALI_END_TOOL");
  return true;
}

static bool test_model_load_defaults() {
  ModelLoad load;
  assert(load.n_ctx == 0);
  assert(load.n_batch == 0);
  assert(load.n_gpu_layers == 0);
  assert(load.rope_scaling_type == 0);
  assert(load.rope_freq_scale == 0.0f);
  assert(load.mtp_enabled == true);
  assert(load.mtp_n_max == 3);
  assert(load.mtp_n_min == 0);
  assert(load.mtp_p_min == 0.0f);
  assert(load.mtp_pos_threshold == 0.30f);
  assert(load.mtp_adaptive == true);
  assert(load.suffix_enabled == false);
  assert(load.suffix_ngram == 4);
  assert(load.suffix_max_draft == 2);
  return true;
}

static bool test_kv_cache_preset_values() {
  // Verify the enum has the expected three values
  assert(static_cast<int>(KVCachePreset::F16) == 0);
  assert(static_cast<int>(KVCachePreset::Balanced) == 1);
  assert(static_cast<int>(KVCachePreset::Compact) == 2);
  return true;
}

static bool test_engine_virtual_dispatch() {
  // Verify virtual dispatch works through Engine*
  Basalt b;
  Engine *e = &b;
  assert(e->supports_rag() == false);
  assert(e->supports_mtp_tuning() == false);
  assert(e->supports_kv_control() == false);
  assert(e->is_gemma_4() == false);
  assert(e->is_can_shift() == false);
  assert(e->mtp_has_best() == false);
  assert(e->memory_kv_percent() == 0.0f);
  assert(e->is_memory_flush() == false);
  return true;
}

// --- qwen35 geometry: prediction layer kind ----------------------------------

static bool test_qwen35_geom_prediction_layer() {
  const char* path = "/tmp/hali_test_qwen35_geom.gguf";
  {
    FILE* f = fopen(path, "wb");
    assert(f);
    // Write a minimal byte so the file is mappable (GgufFile maps before checking virtual registry)
    fputc('x', f);
    fclose(f);
  }

  auto v = std::make_shared<basalt::VirtualGguf>();
  auto u32 = [](uint32_t x) { basalt::MetaValue m; m.type = basalt::MetaType::U32; m.u = x; return m; };
  auto str = [](const char* s) { basalt::MetaValue m; m.type = basalt::MetaType::STRING; m.s = s; return m; };

  v->meta["general.architecture"] = str("qwen35");
  v->meta["qwen35.block_count"] = u32(33);
  v->meta["qwen35.embedding_length"] = u32(4096);
  v->meta["qwen35.feed_forward_length"] = u32(12288);
  v->meta["qwen35.attention.head_count"] = u32(16);
  v->meta["qwen35.attention.head_count_kv"] = u32(4);
  v->meta["qwen35.attention.key_length"] = u32(256);
  v->meta["qwen35.context_length"] = u32(32768);
  v->meta["qwen35.rope.dimension_count"] = u32(128);
  v->meta["qwen35.ssm.state_size"] = u32(128);
  v->meta["qwen35.ssm.group_count"] = u32(16);
  v->meta["qwen35.ssm.time_step_rank"] = u32(32);
  v->meta["qwen35.ssm.conv_kernel"] = u32(4);
  v->meta["qwen35.full_attention_interval"] = u32(4);
  v->meta["qwen35.nextn_predict_layers"] = u32(1);
  basalt::MetaValue toks;
  toks.type = basalt::MetaType::ARRAY;
  toks.elem = basalt::MetaType::STRING;
  toks.count = 100;
  v->meta["tokenizer.ggml.tokens"] = toks;

  basalt::gguf_register_virtual(path, v);

  basalt::GgufFile gg(path);
  std::string err;
  auto g = basalt::models::qwen35::geometry(gg, err);

  assert(g.n_layers == 33);
  assert(g.kinds.size() == 33);

  // Main layers (0..31): 3 GDN + 1 ATTN repeating
  for (int il = 0; il < 32; ++il) {
    const bool expect_gdn = (il + 1) % 4 != 0;
    assert(g.kinds[(size_t) il] == (expect_gdn ? basalt::dense::Mixer::Gdn : basalt::dense::Mixer::Attention));
  }

  // Prediction layer (32) must be Attention, not GDN
  assert(g.kinds[32] == basalt::dense::Mixer::Attention);

  return true;
}

// --- entry point -------------------------------------------------------------

void basalt_test() {
  printf("\nBasalt engine wrapper tests\n");
  printf("=============================\n");

  TEST(constructor_destructor);
  TEST(is_engine);
  TEST(supports_rag_false);
  TEST(supports_mtp_tuning_false);
  TEST(supports_kv_control_false);
  TEST(last_error_empty_initially);
  TEST(memory_info_zeros_before_load);
  TEST(memory_kv_percent_zero);
  TEST(is_memory_flush_false);
  TEST(is_gemma_4_false);
  TEST(is_can_shift_false);
  TEST(mtp_has_best_false);
  TEST(sampling_setters_no_crash);
  TEST(add_stop_no_crash);
  TEST(model_load_defaults);
  TEST(kv_cache_preset_values);
  TEST(engine_virtual_dispatch);
  TEST(qwen35_geom_prediction_layer);

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
}
