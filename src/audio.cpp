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
#include <vector>

#include "miniaudio.h"

namespace audio {

// ── Internal state ──────────────────────────────────────────────────────────

static ma_device g_device = {};
static bool g_device_open = false;
static bool g_enabled = true;
static float g_volume = 0.5f;
static int g_sample_rate = 44100;
static int g_channels = 1;

// Playback buffer state (used by the data callback)
static std::vector<float> g_playback_buffer;
static size_t g_playback_pos = 0;

// ── Melody definitions ──────────────────────────────────────────────────────

// C5-E5-G5-C6 ascending major arpeggio
static const std::vector<std::pair<double, double>> MELODY_COMPLETE = {
  {523.25, 0.25}, {659.25, 0.25}, {783.99, 0.25}, {1046.50, 0.45}
};

// Low A2 buzz
static const std::vector<std::pair<double, double>> MELODY_ERROR = {
  {110.0, 0.4}
};

// Short 880 Hz tick
static const std::vector<std::pair<double, double>> MELODY_TICK = {
  {880.0, 0.12}
};

// A4-E4-A3 descending
static const std::vector<std::pair<double, double>> MELODY_SESSION_END = {
  {440.0, 0.3}, {329.63, 0.3}, {220.0, 0.2}
};

// Soft 1320 Hz ding
static const std::vector<std::pair<double, double>> MELODY_PROMPT = {
  {1318.51, 0.2}
};

// ── Synthesis ───────────────────────────────────────────────────────────────

/**
 * @brief Generate PCM samples for a single note (sine + exponential decay).
 */
static std::vector<float> synth_note(double freq_hz, double duration_s,
                                     int sample_rate, float volume) {
  int n_samples = (int)(duration_s * sample_rate);
  std::vector<float> samples(n_samples, 0.0f);
  double phase = 0.0;
  double phase_increment = 2.0 * M_PI * freq_hz / sample_rate;
  // Exponential decay: alpha such that amplitude reaches ~5% at end
  double alpha = -4.0 / (duration_s * sample_rate);

  for (int i = 0; i < n_samples; i++) {
    double envelope = std::exp(alpha * i);
    samples[i] = (float)(std::sin(phase) * envelope * volume);
    phase += phase_increment;
  }
  return samples;
}

/**
 * @brief Generate PCM samples for a melody (sequence of notes).
 */
static std::vector<float> synth_melody(
    const std::vector<std::pair<double, double>> &notes,
    int sample_rate, float volume) {
  std::vector<float> all_samples;
  for (const auto &note : notes) {
    auto note_samples = synth_note(note.first, note.second, sample_rate, volume);
    all_samples.insert(all_samples.end(), note_samples.begin(), note_samples.end());
  }
  return all_samples;
}

// ── Data callback ───────────────────────────────────────────────────────────

static void data_callback(ma_device *device, void *output,
                          const void *input, ma_uint32 frame_count) {
  (void)device;
  (void)input;

  float *out = static_cast<float *>(output);
  size_t remaining = g_playback_buffer.size() - g_playback_pos;
  size_t to_copy = (remaining < frame_count) ? remaining : frame_count;

  for (ma_uint32 i = 0; i < frame_count; i++) {
    if (i < to_copy) {
      out[i] = g_playback_buffer[g_playback_pos + i];
    } else {
      out[i] = 0.0f;
    }
  }
  g_playback_pos += to_copy;

  // Stop the device when we've played all samples
  if (g_playback_pos >= g_playback_buffer.size()) {
    ma_device_stop(device);
  }
}

// ── Playback ────────────────────────────────────────────────────────────────

static void play_samples(const float *samples, int n_samples) {
  if (!g_device_open || !g_enabled) return;

  // Stop any currently playing sound
  ma_device_stop(&g_device);

  // Load the playback buffer
  g_playback_buffer.assign(samples, samples + n_samples);
  g_playback_pos = 0;

  // Start playback
  ma_device_start(&g_device);

  // Wait for playback to finish (blocking — events are short)
  while (ma_device_get_state(&g_device) == ma_device_state_started) {
    struct timespec ts = {0, 1000000}; // 1 ms
    nanosleep(&ts, nullptr);
  }
}

// ── Public API ──────────────────────────────────────────────────────────────

void init(int sample_rate, int channels) {
  g_sample_rate = sample_rate;
  g_channels = channels;

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.sampleRate = (ma_uint32)sample_rate;
  config.dataCallback = data_callback;
  config.pUserData = nullptr;

  if (ma_device_init(nullptr, &config, &g_device) != MA_SUCCESS) {
    g_device_open = false;
    return;
  }

  g_device_open = true;
}

void shutdown() {
  if (g_device_open) {
    ma_device_uninit(&g_device);
    g_device_open = false;
  }
}

void play(Event event) {
  const std::vector<std::pair<double, double>> *melody = nullptr;
  switch (event) {
    case Event::TaskComplete: melody = &MELODY_COMPLETE; break;
    case Event::TaskError:    melody = &MELODY_ERROR; break;
    case Event::ToolDone:     melody = &MELODY_TICK; break;
    case Event::SessionEnd:   melody = &MELODY_SESSION_END; break;
    case Event::UserPrompt:   melody = &MELODY_PROMPT; break;
  }
  if (!melody) return;

  auto samples = synth_melody(*melody, g_sample_rate, g_volume);
  play_samples(samples.data(), (int)samples.size());
}

void play_tone(double freq_hz, double duration_s, double volume) {
  auto samples = synth_note(freq_hz, duration_s, g_sample_rate, (float)volume);
  play_samples(samples.data(), (int)samples.size());
}

void play_melody(const std::vector<std::pair<double, double>> &notes) {
  auto samples = synth_melody(notes, g_sample_rate, g_volume);
  play_samples(samples.data(), (int)samples.size());
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
