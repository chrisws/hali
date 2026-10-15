// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <iostream>

#include "audio.h"

void audio_test() {
  // ── Default state ──────────────────────────────────────────────────
  assert(audio::is_enabled());
  // No device should be open before init
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
  // These should not crash even without a device
  audio::play_tone(440.0, 0.1, 0.5);
  audio::play(audio::Event::TaskComplete);
  audio::play(audio::Event::TaskError);
  audio::play(audio::Event::ToolDone);
  audio::play(audio::Event::SessionEnd);
  audio::play(audio::Event::UserPrompt);

  auto melody = std::vector<std::pair<double, double>>{
    {523.25, 0.1}, {659.25, 0.1}
  };
  audio::play_melody(melody);

  // ── init / shutdown on headless (no ALSA device) ───────────────────
  // Should not crash; is_available() may be true or false depending
  // on the environment, so we just verify no crash.
  audio::init(44100, 1);
  audio::shutdown();
  assert(!audio::is_available());

  std::cout << "  audio_test: all assertions passed\n";
}