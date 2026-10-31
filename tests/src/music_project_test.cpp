// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <fstream>

#include "music_project.h"

using namespace std;
namespace fs = std::filesystem;

static void test_save_load_roundtrip() {
  // Use a temp directory for test
  string dir = "/tmp/hali_music_test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  // Create a project
  MusicProject proj;
  proj.name = "test_song";
  proj.tempo = 140;

  MusicTrack t1;
  t1.name = "bass";
  t1.script = "C2:0.5 E2:0.5 G2:0.5 C3:0.5";
  t1.volume = 80;
  t1.pan = 0;
  proj.tracks.push_back(t1);

  MusicTrack t2;
  t2.name = "drums";
  t2.script = "K S K H K S K S";
  t2.volume = 70;
  t2.pan = -25;
  proj.tracks.push_back(t2);

  MusicTrack t3;
  t3.name = "melody";
  t3.script = "C4 E4 G4 C5";
  t3.volume = 90;
  t3.pan = 50;
  proj.tracks.push_back(t3);

  // Save
  bool saved = proj.save(dir);
  assert(saved);

  // Verify file exists
  string expected_path = dir + "/test_song.json";
  assert(fs::exists(expected_path));

  // Load
  MusicProject loaded = MusicProject::load(expected_path);
  assert(loaded.name == "test_song");
  assert(loaded.tempo == 140);
  assert(loaded.tracks.size() == 3);

  // Verify track 1
  assert(loaded.tracks[0].name == "bass");
  assert(loaded.tracks[0].script == "C2:0.5 E2:0.5 G2:0.5 C3:0.5");
  assert(loaded.tracks[0].volume == 80);
  assert(loaded.tracks[0].pan == 0);

  // Verify track 2
  assert(loaded.tracks[1].name == "drums");
  assert(loaded.tracks[1].script == "K S K H K S K S");
  assert(loaded.tracks[1].volume == 70);
  assert(loaded.tracks[1].pan == -25);

  // Verify track 3
  assert(loaded.tracks[2].name == "melody");
  assert(loaded.tracks[2].script == "C4 E4 G4 C5");
  assert(loaded.tracks[2].volume == 90);
  assert(loaded.tracks[2].pan == 50);

  // Cleanup
  fs::remove_all(dir);

  cout << "test_save_load_roundtrip passed" << endl;
}

static void test_list() {
  string dir = "/tmp/hali_music_test_list";
  fs::remove_all(dir);
  fs::create_directories(dir);

  // Create two projects
  MusicProject p1;
  p1.name = "alpha";
  p1.save(dir);

  MusicProject p2;
  p2.name = "beta";
  p2.save(dir);

  // List
  auto names = MusicProject::list(dir);
  assert(names.size() == 2);
  assert(names[0] == "alpha");
  assert(names[1] == "beta");

  // Cleanup
  fs::remove_all(dir);

  cout << "test_list passed" << endl;
}

static void test_load_nonexistent() {
  // Loading a non-existent file should return empty project
  MusicProject proj = MusicProject::load("/tmp/nonexistent_music_file.json");
  assert(proj.name.empty());
  assert(proj.tracks.empty());

  cout << "test_load_nonexistent passed" << endl;
}

static void test_empty_project() {
  string dir = "/tmp/hali_music_test_empty";
  fs::remove_all(dir);
  fs::create_directories(dir);

  MusicProject proj;
  proj.name = "empty";
  // No tracks

  bool saved = proj.save(dir);
  assert(saved);

  MusicProject loaded = MusicProject::load(dir + "/empty.json");
  assert(loaded.name == "empty");
  assert(loaded.tempo == 120);
  assert(loaded.tracks.empty());

  fs::remove_all(dir);

  cout << "test_empty_project passed" << endl;
}

static void test_overwrite_project() {
  string dir = "/tmp/hali_music_test_overwrite";
  fs::remove_all(dir);
  fs::create_directories(dir);

  // Create initial project
  MusicProject proj;
  proj.name = "song";
  proj.tempo = 100;
  MusicTrack t;
  t.name = "track1";
  t.script = "C4";
  proj.tracks.push_back(t);
  proj.save(dir);

  // Overwrite with different data
  proj.tempo = 200;
  proj.tracks.clear();
  MusicTrack t2;
  t2.name = "track2";
  t2.script = "D4 E4";
  t2.volume = 50;
  t2.pan = -100;
  proj.tracks.push_back(t2);
  bool saved = proj.save(dir);
  assert(saved);

  // Load and verify overwrite
  MusicProject loaded = MusicProject::load(dir + "/song.json");
  assert(loaded.tempo == 200);
  assert(loaded.tracks.size() == 1);
  assert(loaded.tracks[0].name == "track2");
  assert(loaded.tracks[0].volume == 50);
  assert(loaded.tracks[0].pan == -100);

  fs::remove_all(dir);
  cout << "test_overwrite_project passed" << endl;
}

static void test_list_empty_dir() {
  string dir = "/tmp/hali_music_test_empty_list";
  fs::remove_all(dir);
  fs::create_directories(dir);

  auto names = MusicProject::list(dir);
  assert(names.empty());

  fs::remove_all(dir);
  cout << "test_list_empty_dir passed" << endl;
}

static void test_extreme_values() {
  string dir = "/tmp/hali_music_test_extreme";
  fs::remove_all(dir);
  fs::create_directories(dir);

  MusicProject proj;
  proj.name = "extreme";
  proj.tempo = 300;

  MusicTrack t;
  t.name = "max";
  t.script = "C4";
  t.volume = 100;
  t.pan = 100;
  proj.tracks.push_back(t);

  MusicTrack t2;
  t2.name = "min";
  t2.script = "C4";
  t2.volume = 0;
  t2.pan = -100;
  proj.tracks.push_back(t2);

  proj.save(dir);
  MusicProject loaded = MusicProject::load(dir + "/extreme.json");
  assert(loaded.tempo == 300);
  assert(loaded.tracks[0].volume == 100);
  assert(loaded.tracks[0].pan == 100);
  assert(loaded.tracks[1].volume == 0);
  assert(loaded.tracks[1].pan == -100);

  fs::remove_all(dir);
  cout << "test_extreme_values passed" << endl;
}

static void test_special_chars_in_name() {
  string dir = "/tmp/hali_music_test_special";
  fs::remove_all(dir);
  fs::create_directories(dir);

  MusicProject proj;
  proj.name = "my song (v2)";
  proj.tempo = 120;
  MusicTrack t;
  t.name = "lead-guitar";
  t.script = "C4 D4 E4";
  proj.tracks.push_back(t);

  bool saved = proj.save(dir);
  assert(saved);

  // Load back
  MusicProject loaded = MusicProject::load(dir + "/my song (v2).json");
  assert(loaded.name == "my song (v2)");
  assert(loaded.tracks.size() == 1);
  assert(loaded.tracks[0].name == "lead-guitar");

  fs::remove_all(dir);
  cout << "test_special_chars_in_name passed" << endl;
}

static void test_save_to_preserves_name() {
  // save_to writes to an arbitrary path but must preserve the project name
  string dir = "/tmp/hali_music_test_save_to";
  fs::remove_all(dir);
  fs::create_directories(dir);

  MusicProject proj;
  proj.name = "slow_blues";
  proj.tempo = 72;
  MusicTrack t;
  t.name = "bass";
  t.script = "A2:0.8 A2:0.8";
  proj.tracks.push_back(t);

  // Save to a custom path (simulating _current.json)
  string cur_path = dir + "/_current.json";
  bool saved = proj.save_to(cur_path);
  assert(saved);
  assert(fs::exists(cur_path));

  // Load back — name must be "slow_blues", NOT "_current"
  MusicProject loaded = MusicProject::load(cur_path);
  assert(loaded.name == "slow_blues");
  assert(loaded.tempo == 72);
  assert(loaded.tracks.size() == 1);
  assert(loaded.tracks[0].name == "bass");

  // Original project name unchanged
  assert(proj.name == "slow_blues");

  fs::remove_all(dir);
  cout << "test_save_to_preserves_name passed" << endl;
}

static void test_current_json_name_roundtrip() {
  // Simulates the exact bug scenario:
  // create -> save_to(_current.json) -> load -> name must be real, not "_current"
  string dir = "/tmp/hali_music_test_current";
  fs::remove_all(dir);
  fs::create_directories(dir);

  // Step 1: create project
  MusicProject proj;
  proj.name = "my_song";
  proj.tempo = 100;
  MusicTrack t;
  t.name = "drums";
  t.script = "K S K H";
  proj.tracks.push_back(t);

  // Step 2: save as _current.json (what the dispatch handler does)
  string cur_path = dir + "/_current.json";
  assert(proj.save_to(cur_path));

  // Step 3: load from _current.json (what save/show/add handlers do)
  MusicProject loaded = MusicProject::load(cur_path);
  assert(loaded.name == "my_song");
  assert(loaded.name != "_current");
  assert(!loaded.name.empty());

  // Step 4: modify and save back (what add/set/tempo handlers do)
  loaded.tempo = 120;
  MusicTrack t2;
  t2.name = "bass";
  t2.script = "C2:0.5";
  loaded.tracks.push_back(t2);
  assert(loaded.save(dir));           // saves my_song.json
  assert(loaded.save_to(cur_path));   // saves _current.json

  // Step 5: reload and verify name still intact
  MusicProject reloaded = MusicProject::load(cur_path);
  assert(reloaded.name == "my_song");
  assert(reloaded.tempo == 120);
  assert(reloaded.tracks.size() == 2);

  // Step 6: verify the named file also has the correct name
  MusicProject named = MusicProject::load(dir + "/my_song.json");
  assert(named.name == "my_song");

  fs::remove_all(dir);
  cout << "test_current_json_name_roundtrip passed" << endl;
}

static void test_save_to_creates_parent_dirs() {
  // save_to should create parent directories if they don't exist
  string dir = "/tmp/hali_music_test_nested/a/b";
  fs::remove_all("/tmp/hali_music_test_nested");

  MusicProject proj;
  proj.name = "nested";
  proj.tempo = 90;

  bool saved = proj.save_to(dir + "/nested.json");
  assert(saved);
  assert(fs::exists(dir + "/nested.json"));

  MusicProject loaded = MusicProject::load(dir + "/nested.json");
  assert(loaded.name == "nested");

  fs::remove_all("/tmp/hali_music_test_nested");
  cout << "test_save_to_creates_parent_dirs passed" << endl;
}

void music_project_test() {
  test_save_load_roundtrip();
  test_list();
  test_load_nonexistent();
  test_empty_project();
  test_overwrite_project();
  test_list_empty_dir();
  test_extreme_values();
  test_special_chars_in_name();
  test_save_to_preserves_name();
  test_current_json_name_roundtrip();
  test_save_to_creates_parent_dirs();
}