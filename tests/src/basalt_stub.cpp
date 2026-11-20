// Test stub for Basalt engine - avoids basalt library dependencies.
#include "basalt/basalt.h"

#include <cstring>

// Provide complete types so unique_ptr destructor can be instantiated.
namespace basalt::model {
  struct Started { int _stub; };
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

bool Basalt::do_prefill() {
  return false;
}

int32_t Basalt::do_decode() {
  return -1;
}
