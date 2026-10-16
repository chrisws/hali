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
#include <vector>

#include "miniaudio.h"

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

} // namespace audio