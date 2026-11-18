// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "engine.h"

ModelIter::ModelIter()
  : _engine(nullptr)
  , _t_start(chrono::high_resolution_clock::now())
  , _repetition_count(0)
  , _tokens_generated(0)
  , _has_next(false)
  , _ignore_stop(false) {}

ModelIter::ModelIter(ModelIter &&other) noexcept
  : _engine(other._engine)
  , _last_word(std::move(other._last_word))
  , _tail(std::move(other._tail))
  , _t_start(other._t_start)
  , _repetition_count(other._repetition_count)
  , _tokens_generated(other._tokens_generated)
  , _has_next(other._has_next)
  , _ignore_stop(other._ignore_stop) {
  other._engine = nullptr;
  other._has_next = false;
}