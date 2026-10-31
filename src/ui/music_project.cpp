// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <system_error>
#include <algorithm>

#include "music_project.h"
#include "util/json.h"

namespace fs = std::filesystem;

//
// MusicProject::save
//
bool MusicProject::save(const std::string &dir) const {
  fs::path dir_path(dir);
  std::error_code ec;
  fs::create_directories(dir_path, ec);
  if (ec) {
    return false;
  }

  std::string path = (dir_path / (name + ".json")).string();
  return save_to(path);
}

//
// MusicProject::save_to
//
bool MusicProject::save_to(const std::string &path) const {
  fs::path path_obj(path);
  std::error_code ec;
  fs::create_directories(path_obj.parent_path(), ec);
  if (ec) {
    return false;
  }

  auto doc = json::parse_mutable("");
  if (!doc.is_valid()) {
    return false;
  }

  auto root = doc.get_root();
  root.set_str("name", name);
  root.set_int("tempo", tempo);

  auto tracks_arr = root.set_empty_arr("tracks");
  for (const auto &track : tracks) {
    auto t = tracks_arr.arr_append_obj();
    t.set_str("name", track.name);
    t.set_str("script", track.script);
    t.set_int("volume", track.volume);
    t.set_int("pan", track.pan);
  }

  std::string json_str = doc.to_string(json::WriteFlag::Compact);
  if (json_str.empty()) {
    return false;
  }

  std::ofstream f(path, std::ios::trunc);
  if (!f) {
    return false;
  }
  f << json_str;
  return f.good();
}

//
// MusicProject::load
//
MusicProject MusicProject::load(const std::string &path) {
  MusicProject project;

  std::ifstream f(path);
  if (!f) {
    return project;
  }
  std::ostringstream oss;
  oss << f.rdbuf();
  std::string json_str = oss.str();

  auto doc = json::parse(json_str);
  if (!doc.is_valid()) {
    return project;
  }

  auto root = doc.get_root();
  root.get_str("name", project.name);
  root.get_int("tempo", project.tempo);

  // Parse tracks array
  std::vector<json::JsonValue> track_vals;
  if (root.get_array("tracks", track_vals)) {
    for (const auto &tv : track_vals) {
      MusicTrack track;
      tv.get_str("name", track.name);
      tv.get_str("script", track.script);
      tv.get_int("volume", track.volume);
      tv.get_int("pan", track.pan);
      project.tracks.push_back(track);
    }
  }

  return project;
}

//
// MusicProject::list
//
std::vector<std::string> MusicProject::list(const std::string &dir) {
  std::vector<std::string> names;

  fs::path dir_path(dir);
  if (!fs::exists(dir_path)) {
    return names;
  }

  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(dir_path, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    std::string ext = entry.path().extension().string();
    if (ext == ".json") {
      names.push_back(entry.path().stem().string());
    }
  }

  std::sort(names.begin(), names.end());
  return names;
}