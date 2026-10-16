// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <iostream>
#include <vector>
#include <utility>

#include "audio.h"

void audio_test() {
  // ── Default state ──────────────────────────────────────────────────
  assert(audio::is_enabled());
  assert(!audio::is_available());

  // ── Volume clamping ────────────────────────────────────────────────
  audio::set_volume(0.5f);
  audio::set_volume(-1.0f);   // clamped to 0.0
  audio::set_volume(2.0f);    // clamped to 1.0
  audio::set_volume(0.5f);    // restore

  // ── Enable / disable ───────────────────────────────────────────────
  audio::set_enabled(false);
  assert(!audio::is_enabled());
  audio::set_enabled(true);
  assert(audio::is_enabled());

  // ── Safe no-op when no device ──────────────────────────────────────
  audio::play_tone(440.0, 0.1, 0.5);
  auto melody = std::vector<std::pair<double, double>>{
    {523.25, 0.1}, {659.25, 0.1}
  };
  audio::play_melody(melody);

  // ── init / shutdown round-trip ─────────────────────────────────────
  // Exercises ma_context_init + ma_device_init + ma_context_uninit.
  // On headless systems this may fail gracefully (no device), which
  // is fine — we just verify no crash and clean state afterwards.
  audio::init(44100, 1);
  bool was_available = audio::is_available();
  audio::shutdown();
  assert(!audio::is_available());

  // ── Second round-trip (catches context/device leak) ────────────────
  audio::init(48000, 1);
  (void)audio::is_available();
  audio::shutdown();
  assert(!audio::is_available());

  // ── Play after shutdown must not crash ─────────────────────────────
  audio::play_tone(880.0, 0.05, 0.3);
  audio::play_melody(melody);

  std::cout << "  audio_test: all assertions passed"
            << (was_available ? " (device was available)" : "")
            << "\n";
}