// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "audio.h"

static bool freq_close(double a, double b, double eps = 0.01) {
  return std::fabs(a - b) < eps;
}

// Helper: total note count across all events
static size_t total_notes(const audio::ScriptResult &r) {
  size_t n = 0;
  for (const auto &ev : r.events) n += ev.size();
  return n;
}

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
  audio::play_chord(melody);

  // ── init / shutdown round-trip ─────────────────────────────────────
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
  audio::play_chord(melody);

  // ── play_events: safe no-op when disabled ──────────────────────────
  audio::set_enabled(false);
  audio::play_events({{{440.0, 0.1}}});
  audio::set_enabled(true);

  // ── play_script: empty input ───────────────────────────────────────
  {
    auto r = audio::play_script("");
    assert(!r.error.empty());
    assert(r.events.empty());
  }

  // ── play_script: single note C4 ────────────────────────────────────
  {
    auto r = audio::play_script("C4");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(r.events[0].size() == 1);
    assert(freq_close(r.events[0][0].first, 261.63));   // C4 ≈ 261.63 Hz
    assert(freq_close(r.events[0][0].second, 0.5));     // default: 120bpm, L4 → 0.5s
  }

  // ── play_script: A4 = 440 Hz exactly ───────────────────────────────
  {
    auto r = audio::play_script("A4");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(freq_close(r.events[0][0].first, 440.0));
  }

  // ── play_script: multiple sequential notes ─────────────────────────
  {
    auto r = audio::play_script("C4 E4 G4 C5");
    assert(r.error.empty());
    assert(r.events.size() == 4);
    assert(total_notes(r) == 4);
    assert(freq_close(r.events[0][0].first, 261.63));   // C4
    assert(freq_close(r.events[1][0].first, 329.63));   // E4
    assert(freq_close(r.events[2][0].first, 392.00));   // G4
    assert(freq_close(r.events[3][0].first, 523.25));   // C5
  }

  // ── play_script: sharps and flats ──────────────────────────────────
  {
    auto r = audio::play_script("C#4 Db4");
    assert(r.error.empty());
    assert(r.events.size() == 2);
    assert(freq_close(r.events[0][0].first, 277.18));   // C#4
    assert(freq_close(r.events[1][0].first, 277.18));   // Db4 = C#4
  }

  // ── play_script: explicit duration ─────────────────────────────────
  {
    auto r = audio::play_script("C4:0.25");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(freq_close(r.events[0][0].second, 0.25));
  }

  // ── play_script: tempo affects default duration ────────────────────
  {
    auto r = audio::play_script("T60 C4");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(freq_close(r.events[0][0].second, 1.0));     // 60bpm, L4 → 1.0s
  }

  // ── play_script: note length L2 ────────────────────────────────────
  {
    auto r = audio::play_script("L2 C4");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(freq_close(r.events[0][0].second, 1.0));     // 120bpm, L2 → 1.0s
  }

  // ── play_script: pause token ───────────────────────────────────────
  {
    auto r = audio::play_script("C4 P1 C4");
    assert(r.error.empty());
    assert(r.events.size() == 3);
    assert(r.events[1][0].first == 0.0);                // pause = 0 Hz
    assert(freq_close(r.events[1][0].second, 0.5));     // 1 beat at 120bpm
  }

  // ── play_script: invalid note letter ───────────────────────────────
  {
    auto r = audio::play_script("H4");
    assert(!r.error.empty());
    assert(r.events.empty());
  }

  // ── play_script: unrecognized token ────────────────────────────────
  {
    auto r = audio::play_script("C4 XYZ");
    assert(!r.error.empty());
  }

  // ── play_script: octave range ──────────────────────────────────────
  {
    auto r = audio::play_script("C0 C8");
    assert(r.error.empty());
    assert(r.events.size() == 2);
    assert(freq_close(r.events[0][0].first, 16.35));    // C0 ≈ 16.35 Hz
    assert(freq_close(r.events[1][0].first, 4186.01));  // C8 ≈ 4186.01 Hz
  }

  // ── play_script: simple chord [C4 E4 G4] ───────────────────────────
  {
    auto r = audio::play_script("[C4 E4 G4]");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(r.events[0].size() == 3);
    assert(freq_close(r.events[0][0].first, 261.63));   // C4
    assert(freq_close(r.events[0][1].first, 329.63));   // E4
    assert(freq_close(r.events[0][2].first, 392.00));   // G4
    // All notes share the default duration
    assert(freq_close(r.events[0][0].second, 0.5));
    assert(freq_close(r.events[0][1].second, 0.5));
    assert(freq_close(r.events[0][2].second, 0.5));
  }

  // ── play_script: chord with explicit duration ──────────────────────
  {
    auto r = audio::play_script("[C4 E4]:1.0");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(r.events[0].size() == 2);
    assert(freq_close(r.events[0][0].second, 1.0));
    assert(freq_close(r.events[0][1].second, 1.0));
  }

  // ── play_script: mixed sequential + chord ──────────────────────────
  {
    auto r = audio::play_script("C4 [E4 G4] C5");
    assert(r.error.empty());
    assert(r.events.size() == 3);
    assert(r.events[0].size() == 1);   // C4
    assert(r.events[1].size() == 2);   // E4+G4 chord
    assert(r.events[2].size() == 1);   // C5
    assert(freq_close(r.events[0][0].first, 261.63));
    assert(freq_close(r.events[1][0].first, 329.63));
    assert(freq_close(r.events[1][1].first, 392.00));
    assert(freq_close(r.events[2][0].first, 523.25));
  }

  // ── play_script: two consecutive chords ────────────────────────────
  {
    auto r = audio::play_script("[C4 E4 G4]:1.0 [F4 A4 C5]:1.0");
    assert(r.error.empty());
    assert(r.events.size() == 2);
    assert(r.events[0].size() == 3);
    assert(r.events[1].size() == 3);
    assert(freq_close(r.events[1][0].first, 349.23));   // F4
    assert(freq_close(r.events[1][1].first, 440.00));   // A4
    assert(freq_close(r.events[1][2].first, 523.25));   // C5
  }

  // ── play_script: single note in brackets [C4] ──────────────────────
  {
    auto r = audio::play_script("[C4]");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(r.events[0].size() == 1);
    assert(freq_close(r.events[0][0].first, 261.63));
  }

  // ── play_script: chord with sharps/flats ───────────────────────────
  {
    auto r = audio::play_script("[C#4 Db4]");
    assert(r.error.empty());
    assert(r.events.size() == 1);
    assert(r.events[0].size() == 2);
    assert(freq_close(r.events[0][0].first, 277.18));
    assert(freq_close(r.events[0][1].first, 277.18));
  }

  // ── play_script: chord with pause after ────────────────────────────
  {
    auto r = audio::play_script("[C4 E4] P1 C5");
    assert(r.error.empty());
    assert(r.events.size() == 3);
    assert(r.events[0].size() == 2);   // chord
    assert(r.events[1][0].first == 0.0); // pause
    assert(r.events[2].size() == 1);   // C5
  }

  // ── play_script: empty chord [] → error ────────────────────────────
  {
    auto r = audio::play_script("[]");
    assert(!r.error.empty());
  }

  // ── play_script: unclosed chord → error ────────────────────────────
  {
    auto r = audio::play_script("[C4 E4");
    assert(!r.error.empty());
  }

  // ── play_script: stray close bracket → error ───────────────────────
  {
    auto r = audio::play_script("C4]");
    assert(!r.error.empty());
  }

  // ── play_script: nested brackets → error ───────────────────────────
  {
    auto r = audio::play_script("[[C4] E4]");
    assert(!r.error.empty());
  }

  // ── play_script: chord with invalid note → error ───────────────────
  {
    auto r = audio::play_script("[C4 H4]");
    assert(!r.error.empty());
  }

  std::cout << "  audio_test: all assertions passed"
            << (was_available ? " (device was available)" : "")
            << "\n";
}