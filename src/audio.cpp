// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "audio.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "miniaudio.h"
#include "logging.h"

namespace audio {

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr int SAMPLE_RATE = 48000;
static constexpr float PI2 = 2.0f * M_PI;
static constexpr int MAX_RAMP = 512;
static constexpr int RAMP_SCALE = 20;
static constexpr int MAX_QUEUE_SIZE = 250;

// ── Sound ───────────────────────────────────────────────────────────────────

struct Sound {
  uint32_t total_samples;
  uint32_t sampled;
  uint32_t delay_samples;
  uint32_t faded_in;
  uint32_t fade_out;
  float amplitude;
  float increment;
  float phase;

  Sound(double freq_hz, double duration_s, float volume, uint32_t delay = 0)
    : total_samples((uint32_t)(duration_s * SAMPLE_RATE)),
      sampled(0),
      delay_samples(delay),
      faded_in(0),
      fade_out(0),
      amplitude(volume),
      increment((float)(PI2 * freq_hz / SAMPLE_RATE)),
      phase(0.0f) {
    faded_in = fade_out = std::min((int)total_samples / RAMP_SCALE, MAX_RAMP);
  }

  bool active() const { return sampled < delay_samples + total_samples; }

  float sample() {
    if (sampled < delay_samples) {
      sampled++;
      return 0.0f;
    }
    uint32_t eff = sampled - delay_samples;
    float result = sinf(phase) * amplitude;
    phase = fmodf(phase + increment, PI2);
    sampled++;

    if (faded_in != 0 && eff < faded_in) {
      result *= (float)eff / (float)faded_in;
    } else if (fade_out != 0 && (total_samples - eff) < fade_out) {
      result *= (float)(total_samples - eff) / (float)fade_out;
    }
    return result;
  }
};

// ── State ───────────────────────────────────────────────────────────────────

static ma_context g_context = {};
static ma_device g_device = {};
static bool g_device_open = false;
static bool g_enabled = true;
static float g_volume = 0.5f;

static std::mutex g_lock;
static std::deque<Sound *> g_queue;

// ── Queue helpers ───────────────────────────────────────────────────────────

static void add(double freq_hz, double duration_s, float volume,
                uint32_t delay_samples = 0) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (g_queue.size() >= MAX_QUEUE_SIZE) return;
  g_queue.push_back(new Sound(freq_hz, duration_s, volume * g_volume, delay_samples));
}

static void clear_queue() {
  std::lock_guard<std::mutex> lock(g_lock);
  for (auto *s : g_queue) {
    delete s;
  }
  g_queue.clear();
}

// ── Data callback (runs on the audio thread) ────────────────────────────────

static void data_callback(ma_device *device, void *output,
                          const void *input, ma_uint32 frame_count) {
  (void)input;
  float *out = static_cast<float *>(output);

  // Snapshot active sounds and remove finished ones
  std::vector<Sound *> active;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto it = g_queue.begin(); it != g_queue.end();) {
      if ((*it)->active()) {
        active.push_back(*it);
        ++it;
      } else {
        delete *it;
        it = g_queue.erase(it);
      }
    }
  }

  // Mix all active sounds together with soft-clip compression.
  // tanh() is a smooth soft-clipper: linear below ~0.5, gently
  // compresses above 1.0, avoids harsh clipping artifacts on chords.
  for (ma_uint32 i = 0; i < frame_count; i++) {
    float sum = 0.0f;
    for (auto *s : active) {
      sum += s->sample();
    }
    out[i] = tanhf(sum);
  }
}

// ── Device management ───────────────────────────────────────────────────────

static void ensure_started() {
  if (!g_device_open || !g_enabled) return;
  if (ma_device_get_state(&g_device) != ma_device_state_started) {
    ma_device_start(&g_device);
  }
}

// ── Public API ──────────────────────────────────────────────────────────────

void init(int sample_rate, int channels) {
  (void)sample_rate;
  (void)channels;

  if (ma_context_init(nullptr, 0, nullptr, &g_context) != MA_SUCCESS) {
    return;
  }

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 1;
  config.sampleRate = SAMPLE_RATE;
  config.dataCallback = data_callback;
  config.pUserData = nullptr;

  if (ma_device_init(&g_context, &config, &g_device) != MA_SUCCESS) {
    ma_context_uninit(&g_context);
    g_device_open = false;
    return;
  }
  g_device_open = true;
}

void shutdown() {
  if (g_device_open) {
    ma_device_stop(&g_device);
    ma_device_uninit(&g_device);
    ma_context_uninit(&g_context);
    g_device_open = false;
  }
  clear_queue();
}

void play_tone(double freq_hz, double duration_s, double volume) {
  if (!g_enabled) return;
  add(freq_hz, duration_s, (float)volume);
  ensure_started();
}

void play_events(const std::vector<NoteEvent> &events) {
  if (!g_enabled) return;
  uint32_t delay = 0;
  for (const auto &event : events) {
    double max_dur = 0.0;
    for (const auto &note : event) {
      add(note.first, note.second, 1.0f, delay);
      max_dur = std::max(max_dur, note.second);
    }
    delay += (uint32_t)(max_dur * SAMPLE_RATE);
  }
  ensure_started();
}

void play_melody(const std::vector<std::pair<double, double>> &notes) {
  std::vector<NoteEvent> events;
  events.reserve(notes.size());
  for (const auto &note : notes) {
    events.push_back({note});
  }
  play_events(events);
}

void play_chord(const std::vector<std::pair<double, double>> &notes) {
  play_events({notes});
}

bool is_available() {
  return g_device_open;
}

void set_enabled(bool on) {
  g_enabled = on;
}

void set_volume(float v) {
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  g_volume = v;
}

bool is_enabled() {
  return g_enabled;
}

ScriptResult play_script(const std::string &notes_str) {
  ScriptResult result;

  log_write(LEVEL_DEBUG, "audio", "play_script: [%s]", notes_str.c_str());

  double tempo = 120.0;
  double volume = 0.5;
  double default_len = 4.0;

  auto note_to_semitone = [](char note, char acc) -> int {
    int idx;
    switch (note) {
      case 'C': idx = 0;  break;
      case 'D': idx = 2;  break;
      case 'E': idx = 4;  break;
      case 'F': idx = 5;  break;
      case 'G': idx = 7;  break;
      case 'A': idx = 9;  break;
      case 'B': idx = 11; break;
      default:  return -1;
    }
    if (acc == '#' || acc == '+') idx++;
    else if (acc == '-' || acc == 'b') idx--;
    return idx;
  };

  auto midi_to_freq = [](int midi) -> double {
    return 440.0 * pow(2.0, (midi - 69) / 12.0);
  };

  auto default_duration = [&]() -> double {
    return (60.0 / tempo) * (4.0 / default_len);
  };

  // Parse a single note token (no brackets) into {freq, dur}.
  // Returns false on error.
  auto parse_note = [&](const std::string &tok, double &freq, double &dur) -> bool {
    char c = tok[0];
    if (c < 'A' || c > 'G') return false;
    int pos = 1;
    char acc = 0;
    if (pos < (int)tok.size() && (tok[pos] == '#' || tok[pos] == '-' || tok[pos] == 'b' || tok[pos] == '+')) {
      acc = tok[pos];
      pos++;
    }
    int octave = 4;
    if (pos < (int)tok.size() && isdigit((unsigned char)tok[pos])) {
      octave = tok[pos] - '0';
      pos++;
    }
    dur = default_duration();
    if (pos < (int)tok.size() && tok[pos] == ':') {
      pos++;
      dur = std::stod(tok.substr(pos));
    }
    int semi = note_to_semitone(c, acc);
    if (semi < 0) return false;
    int midi = (octave + 1) * 12 + semi;
    freq = midi_to_freq(midi);
    return true;
  };

  std::istringstream iss(notes_str);
  std::string tok;
  bool in_chord = false;
  NoteEvent chord;

  while (iss >> tok) {
    if (tok.empty()) continue;

    // Strip bracket markers
    if (tok[0] == '[') {
      if (in_chord) {
        result.error = "ERROR: nested chord bracket";
        return result;
      }
      in_chord = true;
      chord.clear();
      tok = tok.substr(1);
      if (tok.empty()) continue;
    }
    bool close_chord = false;
    double chord_dur_override = -1.0;
    size_t close_pos = tok.find(']');
    if (close_pos != std::string::npos) {
      if (!in_chord) {
        result.error = "ERROR: unexpected ']': " + tok;
        return result;
      }
      close_chord = true;
      if (close_pos + 1 < tok.size() && tok[close_pos + 1] == ':') {
        chord_dur_override = std::stod(tok.substr(close_pos + 2));
      }
      tok = tok.substr(0, close_pos);
      if (tok.empty()) {
        // was just "]" with no note
        if (chord.empty()) {
          result.error = "ERROR: empty chord";
          return result;
        }
        if (chord_dur_override >= 0.0) {
          for (auto &n : chord) n.second = chord_dur_override;
        }
        result.events.push_back(chord);
        in_chord = false;
        continue;
      }
    }

    char c = tok[0];

    // Control tokens (only valid outside chords)
    if (!in_chord) {
      if (c == 'T' && tok.size() > 1) {
        tempo = std::stod(tok.substr(1));
        continue;
      }
      if (c == 'V' && tok.size() > 1) {
        volume = std::stod(tok.substr(1)) / 100.0;
        continue;
      }
      if (c == 'L' && tok.size() > 1) {
        default_len = std::stod(tok.substr(1));
        continue;
      }
      if (c == 'P' && tok.size() > 1) {
        double beats = std::stod(tok.substr(1));
        double dur = (60.0 / tempo) * beats;
        result.events.push_back({{0.0, dur}});
        if (close_chord) { in_chord = false; }
        continue;
      }
    }

    // Note token
    double freq = 0.0, dur = 0.0;
    if (!parse_note(tok, freq, dur)) {
      result.error = "ERROR: invalid note: " + tok;
      return result;
    }

    if (in_chord) {
      chord.emplace_back(freq, dur);
    } else {
      result.events.push_back({{freq, dur}});
    }

    if (close_chord) {
      if (chord_dur_override >= 0.0) {
        for (auto &n : chord) n.second = chord_dur_override;
      }
      result.events.push_back(chord);
      in_chord = false;
    }
  }

  if (in_chord) {
    result.error = "ERROR: unclosed chord bracket";
    return result;
  }

  if (result.events.empty()) {
    result.error = "ERROR: no notes to play";
    return result;
  }

  size_t total_notes = 0;
  for (const auto &ev : result.events) total_notes += ev.size();
  log_write(LEVEL_DEBUG, "audio", "play_script: %zu events (%zu notes), tempo=%.0f, volume=%.2f, default_len=%.1f",
            result.events.size(), total_notes, tempo, volume, default_len);

  return result;
}

void play_test() {
  // C-F-G-C progression with register shift and 4-note variation on the final chord
  static const NoteEvent events[] = {
    { {261.63, 0.4}, {329.63, 0.4}, {392.00, 0.4} },              // C major
    { {349.23, 0.4}, {440.00, 0.4}, {523.25, 0.4} },              // F major
    { {392.00, 0.4}, {493.88, 0.4}, {587.33, 0.4} },              // G major
    { {523.25, 0.6}, {659.25, 0.6}, {783.99, 0.6}, {1046.50, 0.6} }, // C major (high, 4-note)
  };
  play_events(std::vector<NoteEvent>(std::begin(events), std::end(events)));
}

void play_tool_tick() {
  // E6 → G6: quick bright ascending blip (Mario coin-pickup style)
  static const NoteEvent events[] = {
    { {1318.51, 0.04} },
    { {1568.00, 0.04} },
  };
  play_events(std::vector<NoteEvent>(std::begin(events), std::end(events)));
}

void play_user_prompt() {
  // C6 → E6: warm inviting "ding" (Monkey Island new-quest style)
  static const NoteEvent events[] = {
    { {1046.50, 0.10} },
    { {1318.51, 0.15} },
  };
  play_events(std::vector<NoteEvent>(std::begin(events), std::end(events)));
}

} // namespace audio