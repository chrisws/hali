// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "audio.h"

#include <cmath>
#include <cstring>
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
  uint32_t faded_in;
  uint32_t fade_out;
  float amplitude;
  float increment;

  Sound(double freq_hz, double duration_s, float volume)
    : total_samples((uint32_t)(duration_s * SAMPLE_RATE)),
      sampled(0),
      faded_in(0),
      fade_out(0),
      amplitude(volume),
      increment((float)(PI2 * freq_hz / SAMPLE_RATE)) {
    faded_in = fade_out = std::min((int)total_samples / RAMP_SCALE, MAX_RAMP);
  }

  bool ready() const { return sampled < total_samples; }

  float sample(float &phase) {
    float result = sinf(phase) * amplitude;
    sampled++;
    phase = fmodf(phase + increment, PI2);

    if (faded_in != 0 && sampled < faded_in) {
      result *= (float)sampled / (float)faded_in;
    } else if (fade_out != 0 && (total_samples - sampled) < fade_out) {
      result *= (float)(total_samples - sampled) / (float)fade_out;
    }
    return result;
  }

  void sync(Sound *prev) {
    if (prev && prev->increment == increment) {
      faded_in = 0;
      prev->fade_out = 0;
    }
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
static float g_phase = 0.0f;

// ── Queue helpers ───────────────────────────────────────────────────────────

static Sound *front() {
  std::lock_guard<std::mutex> lock(g_lock);
  Sound *result = nullptr;
  while (!g_queue.empty()) {
    result = g_queue.front();
    if (result->ready()) {
      break;
    }
    delete result;
    g_queue.pop_front();
    result = nullptr;
  }
  return result;
}

static void add(double freq_hz, double duration_s, float volume) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (g_queue.size() >= MAX_QUEUE_SIZE) return;
  Sound *prev = g_queue.empty() ? nullptr : g_queue.back();
  Sound *sound = new Sound(freq_hz, duration_s, volume * g_volume);
  g_queue.push_back(sound);
  if (prev) {
    sound->sync(prev);
  }
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

  Sound *sound = front();
  for (ma_uint32 i = 0; i < frame_count; i++) {
    if (sound == nullptr) {
      out[i] = 0.0f;
    } else {
      out[i] = sound->sample(g_phase);
      if (!sound->ready()) {
        sound = front();
      }
    }
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
  g_phase = 0.0f;
}

void play_tone(double freq_hz, double duration_s, double volume) {
  if (!g_enabled) return;
  add(freq_hz, duration_s, (float)volume);
  ensure_started();
}

void play_melody(const std::vector<std::pair<double, double>> &notes) {
  if (!g_enabled) return;
  for (const auto &note : notes) {
    add(note.first, note.second, 1.0f);
  }
  ensure_started();
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

  std::istringstream iss(notes_str);
  std::string tok;
  while (iss >> tok) {
    if (tok.empty()) continue;
    char c = tok[0];

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
      result.melody.emplace_back(0.0, dur);
      continue;
    }

    if (c >= 'A' && c <= 'G') {
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
      double dur = default_duration();
      if (pos < (int)tok.size() && tok[pos] == ':') {
        pos++;
        dur = std::stod(tok.substr(pos));
      }
      int semi = note_to_semitone(c, acc);
      if (semi < 0) {
        result.error = "ERROR: invalid note: " + tok;
        return result;
      }
      int midi = (octave + 1) * 12 + semi;
      result.melody.emplace_back(midi_to_freq(midi), dur);
      continue;
    }

    result.error = "ERROR: unrecognized token: " + tok;
    return result;
  }

  if (result.melody.empty()) {
    result.error = "ERROR: no notes to play";
    return result;
  }

  log_write(LEVEL_DEBUG, "audio", "play_script: %zu notes, tempo=%.0f, volume=%.2f, default_len=%.1f",
            result.melody.size(), tempo, volume, default_len);

  return result;
}

} // namespace audio