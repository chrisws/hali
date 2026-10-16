// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef AUDIO_H
#define AUDIO_H

#include <vector>
#include <utility>

namespace audio {

/**
 * @brief Initialize the audio subsystem.
 * Safe to call on headless systems — silently no-ops if no device found.
 */
void init(int sample_rate = 44100, int channels = 1);

/** @brief Shut down the audio device and free resources. */
void shutdown();

/** @brief Play a single sine tone with exponential decay. */
void play_tone(double freq_hz, double duration_s, double volume = 0.5);

/**
 * @brief Play a sequence of notes.
 * @param notes vector of {freq_hz, duration_s} pairs, played sequentially.
 */
void play_melody(const std::vector<std::pair<double, double>> &notes);

/** @brief Returns true if an audio output device was found. */
bool is_available();

/** @brief Enable or disable all audio output. */
void set_enabled(bool on);

/** @brief Set master volume (0.0 .. 1.0). */
void set_volume(float v);

/** @brief Returns true if audio is currently enabled. */
bool is_enabled();

} // namespace audio

#endif