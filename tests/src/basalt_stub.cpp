// Test stub for Basalt engine - avoids basalt library dependencies.
#include "basalt/basalt.h"

#include <cstring>

// (Started is now provided by the real header via basalt/dense/model.hpp include below)
namespace basalt::program {
  class Tokenizer {
  public:
    ~Tokenizer() = default;
  };
}

Basalt::Basalt() = default;
Basalt::~Basalt() = default;

void Basalt::set_error(const char *fmt, ...) {
  (void)fmt;
  _last_error[0] = '\0';
}

const char *Basalt::last_error() const {
  return _last_error;
}

bool Basalt::load_model(const ModelLoad &load) {
  (void)load;
  return false;
}

void Basalt::reset() {}

void Basalt::add_stop(const char *stop) {
  if (stop) {
    _stops.push_back(stop);
  }
}

void Basalt::set_max_tokens(int n) { _max_tokens = n; }
void Basalt::set_temperature(float v) { _sampling.temperature = v; }
void Basalt::set_top_k(int v) { _sampling.top_k = v; }
void Basalt::set_top_p(float v) { _sampling.top_p = v; }
void Basalt::set_min_p(float v) { _sampling.min_p = v; }
void Basalt::set_penalty_repeat(float v) { _sampling.penalty_repeat = v; }
void Basalt::set_penalty_freq(float v) { _sampling.penalty_freq = v; }
void Basalt::set_penalty_present(float v) { _sampling.penalty_present = v; }
void Basalt::set_penalty_last_n(int32_t v) { _sampling.penalty_last_n = v; }
void Basalt::set_seed(unsigned int v) { _sampling.seed = v; }
void Basalt::set_log_level(int level) { (void)level; }

bool Basalt::add_message(ModelIter &iter, const string &role,
                         const string &content) {
  (void)iter;
  (void)role;
  (void)content;
  return false;
}

string Basalt::next(ModelIter &iter) {
  (void)iter;
  return "";
}

ModelMemoryInfo Basalt::memory_info() const {
  return ModelMemoryInfo{};
}

float Basalt::memory_kv_percent() const {
  return 0.0f;
}

bool Basalt::is_memory_flush() const {
  return false;
}

bool Basalt::load_tokenizer() {
  return false;
}

bool Basalt::do_prefill() {
  return false;
}

int32_t Basalt::do_decode() {
  return -1;
}

// Stub for Geometry::check() - basic structural validation without kernel deps.
#include "basalt/dense/model.hpp"

namespace basalt::dense {
std::string Geometry::check() const {
    if ((int) kinds.size() != n_layers || n_layers <= 0) return "the layer kinds do not cover the layers";
    if (n_embd % 256 || n_ff % 256) return "widths must be whole 256-value blocks";
    if (attn_layers() > 0) {
        if (head_dim != 256) return "attention heads must be 256 wide";
        if (n_kv <= 0 || n_head % n_kv || n_head / n_kv > 8) return "attention head grouping not supported";
        if (rope.rot <= 0 || rope.rot > head_dim || rope.rot % 2 || rope.rot / 2 > 64) return "rotary width not supported";
    }
    // Skip kernel-specific checks (gdn_prompt_supported, dec_gdn_rec_supported)
    return {};
}
}  // namespace basalt::dense

// Stubs for non-inline Geometry methods
namespace basalt::dense {
int Geometry::gdn_layers() const { return (int) std::count(kinds.begin(), kinds.end(), Mixer::Gdn); }
int Geometry::attn_layers() const { return (int) std::count(kinds.begin(), kinds.end(), Mixer::Attention); }
}  // namespace basalt::dense

// Stub for the basalt platform's fwrite wrapper
namespace std {
size_t hali_basalt_fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}
}  // namespace std
