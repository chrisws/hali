// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef AUDIO_H
#define AUDIO_H

#include <string>
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

/**
 * @brief Play multiple notes simultaneously (a chord).
 * @param notes vector of {freq_hz, duration_s} pairs, all starting together.
 */
void play_chord(const std::vector<std::pair<double, double>> &notes);

/**
 * @brief A single playback event: one or more simultaneous notes.
 *        A single-note event is sequential; multi-note events are chords.
 */
using NoteEvent = std::vector<std::pair<double, double>>;

/**
 * @brief Play a sequence of events. Each event contains one or more
 *        simultaneous notes. Events play sequentially; notes within
 *        an event play together.
 * @param events vector of NoteEvent (each a vector of {freq_hz, duration_s}).
 */
void play_events(const std::vector<NoteEvent> &events);

/** @brief Returns true if an audio output device was found. */
bool is_available();

/** @brief Enable or disable all audio output. */
void set_enabled(bool on);

/** @brief Set master volume (0.0 .. 1.0). */
void set_volume(float v);

/** @brief Returns true if audio is currently enabled. */
bool is_enabled();

/**
 * @brief Result of parsing a note script.
 */
struct ScriptResult {
  std::string error;
  std::vector<NoteEvent> events;
};

/**
 * @brief Parse a note script string into a melody.
 * Format: space-separated tokens.
 *   Notes:  C4  D#5  E-3  A4:0.5   (letter [accidental] [octave] [:duration])
 *   Control: T120 (tempo BPM)  V50 (volume 0-100)  L4 (note length)  P2 (pause beats)
 * @return Parsed melody and/or error message.
 */
ScriptResult play_script(const std::string &notes_str);

} // namespace audio

#endif