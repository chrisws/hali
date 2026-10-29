// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef MUSIC_PROJECT_H
#define MUSIC_PROJECT_H

#include <string>
#include <vector>

/**
 * @brief A single track in a multi-track music project.
 */
struct MusicTrack {
  std::string name;
  std::string script;
  int volume = 80;    // 0-100
  int pan = 0;        // -100 (full left) .. +100 (full right)
};

/**
 * @brief A multi-track music project with persistence.
 */
struct MusicProject {
  std::string name;
  int tempo = 120;
  std::vector<MusicTrack> tracks;

  /**
   * @brief Save the project to <dir>/<name>.json.
   * @param dir  Directory to save into (created if it doesn't exist).
   * @return true on success.
   */
  bool save(const std::string &dir) const;

  /**
   * @brief Load a project from a JSON file.
   * @param path  Full path to the .json file.
   * @return The loaded project (empty name on failure).
   */
  static MusicProject load(const std::string &path);

  /**
   * @brief List all project names in a directory.
   * @param dir  Directory containing .json project files.
   * @return Vector of project names (without .json extension).
   */
  static std::vector<std::string> list(const std::string &dir);
};

#endif