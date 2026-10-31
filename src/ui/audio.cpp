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
#include <cstdint>
#include <cctype>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <ios>

#include "miniaudio.h"
#include "logging.h"

namespace audio {

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr int SAMPLE_RATE = 48000;
static constexpr float PI2 = 2.0f * M_PI;
static constexpr int MAX_RAMP = 512;
static constexpr int RAMP_SCALE = 20;
static constexpr int MAX_QUEUE_SIZE = 250;

// ── Crossfade curves ────────────────────────────────────────────────────────
// Three-phase: ramp → harmonise (hold) → ramp
// During the "harmonise" plateau both tones are at H, giving a brief
// moment where both chords are clearly present together.

static constexpr float HARMONISE_LEVEL = 0.7f;
static constexpr float PHASE_RAMP     = 0.35f;  // 35% ramp
static constexpr float PHASE_HOLD     = 0.30f;  // 30% hold
// Phase 3 = 1.0 - PHASE_RAMP - PHASE_HOLD = 35% ramp

static float fade_in_curve(float t) {
  // t: 0→1.  Amplitude: 0 → H → 1
  float p1 = PHASE_RAMP;
  float p2 = p1 + PHASE_HOLD;
  if (t < p1) {
    return HARMONISE_LEVEL * (t / p1);
  } else if (t < p2) {
    return HARMONISE_LEVEL;
  } else {
    return HARMONISE_LEVEL + (1.0f - HARMONISE_LEVEL) * ((t - p2) / (1.0f - p2));
  }
}

static float fade_out_curve(float t) {
  // t: 0→1.  Amplitude: 1 → H → 0
  float p1 = PHASE_RAMP;
  float p2 = p1 + PHASE_HOLD;
  if (t < p1) {
    return 1.0f - (1.0f - HARMONISE_LEVEL) * (t / p1);
  } else if (t < p2) {
    return HARMONISE_LEVEL;
  } else {
    return HARMONISE_LEVEL * ((1.0f - t) / (1.0f - p2));
  }
}

// ── Sound ───────────────────────────────────────────────────────────────────

struct Sound {
  uint32_t total_samples;
  uint32_t sampled;
  uint32_t delay_samples;
  uint32_t fade_in_samples;
  uint32_t fade_out_samples;
  float amplitude;
  float increment;
  float phase;
  float pan;  // -1.0 (full left) .. +1.0 (full right), 0 = centre

  Sound(double freq_hz, double duration_s, float volume, uint32_t delay = 0,
        uint32_t fade_in = 0, uint32_t fade_out = 0, float pan = 0.0f)
    : total_samples((uint32_t)(duration_s * SAMPLE_RATE)),
      sampled(0),
      delay_samples(delay),
      fade_in_samples(fade_in),
      fade_out_samples(fade_out),
      amplitude(volume),
      increment((float)(PI2 * freq_hz / SAMPLE_RATE)),
      phase(0.0f),
      pan(pan) {
    if (fade_in_samples == 0)
      fade_in_samples = std::min((int)total_samples / RAMP_SCALE, MAX_RAMP);
    if (fade_out_samples == 0)
      fade_out_samples = std::min((int)total_samples / RAMP_SCALE, MAX_RAMP);
  }

  bool active() const { return sampled < delay_samples + total_samples; }

  float sample() {
    if (sampled < delay_samples) {
      sampled++;
      return 0.0f;
    }
    uint32_t eff = sampled - delay_samples;
    if (eff >= total_samples) {
      sampled++;
      return 0.0f;
    }
    float result = sinf(phase) * amplitude;
    phase = fmodf(phase + increment, PI2);
    sampled++;

    // Exponential decay matching save_wav: exp(-3t/T)
    // Brings amplitude to ~5% by the end of the note, giving a
    // natural fade-out so consecutive chords blend smoothly.
    float decay = expf(-3.0f * (float)eff / (float)total_samples);
    result *= decay;

    if (fade_in_samples != 0 && eff < fade_in_samples) {
      float t = (float)eff / (float)fade_in_samples;
      result *= fade_in_curve(t);
    } else if (fade_out_samples != 0 && (total_samples - eff) < fade_out_samples) {
      float t = (float)(eff - (total_samples - fade_out_samples)) / (float)fade_out_samples;
      result *= fade_out_curve(t);
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
                uint32_t delay_samples = 0, uint32_t fade_in = 0,
                uint32_t fade_out = 0, float pan = 0.0f) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (g_queue.size() >= MAX_QUEUE_SIZE) return;
  g_queue.push_back(new Sound(freq_hz, duration_s, volume * g_volume,
                             delay_samples, fade_in, fade_out, pan));
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

  // Mix all active sounds into stereo with equal-power panning.
  // tanh() is a smooth soft-clipper: linear below ~0.5, gently
  // compresses above 1.0, avoids harsh clipping artifacts on chords.
  for (ma_uint32 i = 0; i < frame_count; i++) {
    float sum_l = 0.0f;
    float sum_r = 0.0f;
    for (auto *s : active) {
      float mono = s->sample();
      // Equal-power pan: pan=-1 → full left, pan=+1 → full right
      float gain_l = cosf((s->pan + 1.0f) * M_PI / 4.0f);
      float gain_r = sinf((s->pan + 1.0f) * M_PI / 4.0f);
      sum_l += mono * gain_l;
      sum_r += mono * gain_r;
    }
    out[2 * i]     = tanhf(sum_l);
    out[2 * i + 1] = tanhf(sum_r);
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
  config.playback.channels = 2;
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

  // Compute crossfade duration: 50ms or 25% of shortest chord, whichever is less.
  // This ensures the crossfade is short enough not to muddy the transition
  // but long enough to eliminate the click from the dip-to-silence.
  double min_dur = 1e9;
  for (const auto &event : events) {
    for (const auto &note : event) {
      min_dur = std::min(min_dur, note.second);
    }
  }
  uint32_t crossfade = (uint32_t)(std::min(0.05, min_dur * 0.25) * SAMPLE_RATE);

  uint32_t delay = 0;
  for (size_t i = 0; i < events.size(); i++) {
    const auto &event = events[i];
    double max_dur = 0.0;
    for (const auto &note : event) {
      max_dur = std::max(max_dur, note.second);
    }

    // First chord: fade-in only (auto). Last chord: fade-out only (auto).
    // Middle chords: crossfade on both edges.
    uint32_t fade_in = (i == 0) ? 0 : crossfade;
    uint32_t fade_out = (i == events.size() - 1) ? 0 : crossfade;

    for (const auto &note : event) {
      add(note.first, note.second, 1.0f, delay, fade_in, fade_out);
    }

    // Next chord starts `crossfade` samples before this one ends,
    // creating an overlap where both are active and blending.
    delay += (uint32_t)(max_dur * SAMPLE_RATE) - crossfade;
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

void play_mix(const std::vector<TrackMix> &tracks) {
  if (!g_enabled) return;

  for (const auto &track : tracks) {
    uint32_t delay = 0;
    for (size_t i = 0; i < track.events.size(); i++) {
      const auto &event = track.events[i];
      double max_dur = 0.0;
      for (const auto &note : event) {
        max_dur = std::max(max_dur, note.second);
      }
      for (const auto &note : event) {
        add(note.first, note.second, track.volume, delay, 0, 0, track.pan);
      }
      delay += (uint32_t)(max_dur * SAMPLE_RATE);
    }
  }
  ensure_started();
}

bool is_available() {
  return g_device_open;
}

void stop() {
  clear_queue();
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
      if (c == 'T' && tok.size() > 1 && isdigit((unsigned char)tok[1])) {
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

    // Drum tokens: K(kick) S(snare) H(hi-hat) O(open hat) T(tom)
    // These work both inside and outside chords.
    // Format: single letter, optionally followed by :duration
    {
      double drum_freq = 0.0;
      bool is_drum = false;
      switch (c) {
        case 'K': drum_freq = 60.0;   is_drum = true; break;  // kick
        case 'S': drum_freq = 200.0;  is_drum = true; break;  // snare
        case 'H': drum_freq = 8000.0; is_drum = true; break;  // closed hi-hat
        case 'O': drum_freq = 7000.0; is_drum = true; break;  // open hi-hat
        case 'T': drum_freq = 150.0;  is_drum = true; break;  // tom
        default:  break;
      }
      if (is_drum) {
        double drum_dur = default_duration();
        size_t colon = tok.find(':');
        if (colon != std::string::npos) {
          drum_dur = std::stod(tok.substr(colon + 1));
        }
        if (in_chord) {
          chord.emplace_back(drum_freq, drum_dur);
        } else {
          result.events.push_back({{drum_freq, drum_dur}});
        }
        if (close_chord) {
          result.events.push_back(chord);
          in_chord = false;
        }
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

bool save_wav(const std::vector<NoteEvent> &events, const std::string &path, int sample_rate) {
  // Render all events into a single sample buffer
  std::vector<float> samples;
  for (const auto &ev : events) {
    int n_samples = 0;
    for (const auto &note : ev) {
      n_samples = std::max(n_samples, static_cast<int>(note.second * sample_rate));
    }
    if (n_samples == 0) continue;
    for (int i = 0; i < n_samples; ++i) {
      float t = static_cast<float>(i) / sample_rate;
      float sum = 0.0f;
      for (const auto &note : ev) {
        if (t < note.second) {
          double decay = std::exp(-3.0 * t / note.second);
          sum += static_cast<float>(std::sin(2.0 * M_PI * note.first * t) * decay);
        }
      }
      samples.push_back(sum / static_cast<float>(ev.size()));
    }
  }

  if (samples.empty()) return false;

  // Build stereo WAV: centre-panned (same signal on L and R)
  std::vector<int16_t> pcm_data(samples.size() * 2);
  for (size_t i = 0; i < samples.size(); ++i) {
    float clamped = std::clamp(samples[i], -1.0f, 1.0f);
    int16_t val = static_cast<int16_t>(clamped * 32767.0f);
    pcm_data[i * 2]     = val;  // L
    pcm_data[i * 2 + 1] = val;  // R
  }

  const uint32_t data_size = static_cast<uint32_t>(pcm_data.size() * sizeof(int16_t));
  const uint32_t file_size = 36 + data_size;
  const uint16_t audio_format = 1;   // PCM
  const uint16_t channels = 2;
  const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * 2);
  const uint16_t block_align = channels * 2;
  const uint16_t bits_per_sample = 16;

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;

  // RIFF header
  f.write("RIFF", 4);
  f.write(reinterpret_cast<const char *>(&file_size), 4);
  f.write("WAVE", 4);

  // fmt chunk
  f.write("fmt ", 4);
  uint32_t fmt_size = 16;
  f.write(reinterpret_cast<const char *>(&fmt_size), 4);
  f.write(reinterpret_cast<const char *>(&audio_format), 2);
  f.write(reinterpret_cast<const char *>(&channels), 2);
  f.write(reinterpret_cast<const char *>(&sample_rate), 4);
  f.write(reinterpret_cast<const char *>(&byte_rate), 4);
  f.write(reinterpret_cast<const char *>(&block_align), 2);
  f.write(reinterpret_cast<const char *>(&bits_per_sample), 2);

  // data chunk
  f.write("data", 4);
  f.write(reinterpret_cast<const char *>(&data_size), 4);
  f.write(reinterpret_cast<const char *>(pcm_data.data()), data_size);

  f.close();
  return f.good();
}

bool save_mix_wav(const std::vector<TrackMix> &tracks, const std::string &path, int sample_rate) {
  if (tracks.empty()) return false;

  // Determine total duration in samples
  uint32_t total_samples = 0;
  for (const auto &track : tracks) {
    uint32_t track_end = 0;
    for (const auto &event : track.events) {
      double max_dur = 0.0;
      for (const auto &note : event) {
        max_dur = std::max(max_dur, note.second);
      }
      track_end += (uint32_t)(max_dur * sample_rate);
    }
    total_samples = std::max(total_samples, track_end);
  }

  if (total_samples == 0) return false;

  // Allocate stereo buffer
  std::vector<float> buf_l(total_samples, 0.0f);
  std::vector<float> buf_r(total_samples, 0.0f);

  // Render each track
  for (const auto &track : tracks) {
    uint32_t offset = 0;
    for (const auto &event : track.events) {
      double max_dur = 0.0;
      for (const auto &note : event) {
        max_dur = std::max(max_dur, note.second);
      }
      uint32_t event_samples = (uint32_t)(max_dur * sample_rate);

      // Equal-power pan gains
      float gain_l = cosf((track.pan + 1.0f) * M_PI / 4.0f);
      float gain_r = sinf((track.pan + 1.0f) * M_PI / 4.0f);

      for (uint32_t i = 0; i < event_samples; i++) {
        float t = (float)i / sample_rate;
        float sum = 0.0f;
        for (const auto &note : event) {
          if (t < note.second && note.first > 0.0) {
            double decay = std::exp(-3.0 * t / note.second);
            sum += static_cast<float>(std::sin(2.0 * M_PI * note.first * t) * decay);
          }
        }
        // Normalise by note count, apply track volume and pan
        float val = (sum / static_cast<float>(event.size())) * track.volume;
        uint32_t idx = offset + i;
        if (idx < total_samples) {
          buf_l[idx] += val * gain_l;
          buf_r[idx] += val * gain_r;
        }
      }
      offset += event_samples;
    }
  }

  // Soft-clip with tanh
  for (uint32_t i = 0; i < total_samples; i++) {
    buf_l[i] = tanhf(buf_l[i]);
    buf_r[i] = tanhf(buf_r[i]);
  }

  // Convert to 16-bit PCM
  std::vector<int16_t> pcm_data(total_samples * 2);
  for (uint32_t i = 0; i < total_samples; i++) {
    pcm_data[i * 2]     = static_cast<int16_t>(buf_l[i] * 32767.0f);
    pcm_data[i * 2 + 1] = static_cast<int16_t>(buf_r[i] * 32767.0f);
  }

  // Write WAV file
  const uint32_t data_size = static_cast<uint32_t>(pcm_data.size() * sizeof(int16_t));
  const uint32_t file_size = 36 + data_size;
  const uint16_t audio_format = 1;
  const uint16_t channels = 2;
  const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * 2);
  const uint16_t block_align = channels * 2;
  const uint16_t bits_per_sample = 16;

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;

  f.write("RIFF", 4);
  f.write(reinterpret_cast<const char *>(&file_size), 4);
  f.write("WAVE", 4);

  f.write("fmt ", 4);
  uint32_t fmt_size = 16;
  f.write(reinterpret_cast<const char *>(&fmt_size), 4);
  f.write(reinterpret_cast<const char *>(&audio_format), 2);
  f.write(reinterpret_cast<const char *>(&channels), 2);
  f.write(reinterpret_cast<const char *>(&sample_rate), 4);
  f.write(reinterpret_cast<const char *>(&byte_rate), 4);
  f.write(reinterpret_cast<const char *>(&block_align), 2);
  f.write(reinterpret_cast<const char *>(&bits_per_sample), 2);

  f.write("data", 4);
  f.write(reinterpret_cast<const char *>(&data_size), 4);
  f.write(reinterpret_cast<const char *>(pcm_data.data()), data_size);

  f.close();
  return f.good();
}

} // namespace audio