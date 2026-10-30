// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "knowledge.h"
#include "llama_sb_rag.h"

namespace fs = std::filesystem;

//
// Helper: create a minimal .rag file at the given path
//
static void make_rag_file(const std::string &path, const std::string &name) {
  RagDB db;
  db.embed_dim = 4;
  RagChunk c;
  c.text = "test content for " + name + " with enough text to be valid";
  c.source = "test.cpp";
  c.type = "function";
  c.embedding = {0.1f, 0.2f, 0.3f, 0.4f};
  db.chunks.push_back(c);
  db.save(path);
}

//
// test: zim_path and rag_path generate correct paths
//
static void test_km_paths() {
  KnowledgeManager km;
  km.set_rag_dir("/tmp/hali_km_test/rag");
  km.set_zim_dir("/tmp/hali_km_test/zim");

  assert(km.rag_path("wikipedia") == "/tmp/hali_km_test/rag/wikipedia.rag");
  assert(km.zim_path("wikipedia") == "/tmp/hali_km_test/zim/wikipedia.zim");

  std::cout << "test_km_paths passed" << std::endl;
}

//
// test: load_meta parses the bundled kiwix-meta.json
//
static void test_km_load_meta() {
  KnowledgeManager km;
  bool ok = km.load_meta("../../share/kiwix-meta.json");
  assert(ok);

  /* Should have multiple sources */
  std::string list = km.list_sources();
  assert(!list.empty());
  assert(list.size() > 100); /* definitely more than a trivial string */

  std::cout << "test_km_load_meta passed" << std::endl;
}

//
// test: find a known source by id
//
static void test_km_find() {
  KnowledgeManager km;
  km.load_meta("../../share/kiwix-meta.json");

  /* Find the first source from the list */
  std::string list = km.list_sources();
  assert(!list.empty());

  /* Try a well-known id that should exist in the meta file */
  const KnowledgeSource *src = km.find("wikipedia_en");
  if (src) {
    assert(!src->id.empty());
    assert(!src->title.empty());
    assert(!src->url.empty());
  }

  std::cout << "test_km_find passed" << std::endl;
}

//
// test: find a non-existent source returns nullptr
//
static void test_km_find_missing() {
  KnowledgeManager km;
  km.load_meta("../../share/kiwix-meta.json");

  const KnowledgeSource *src = km.find("this_id_does_not_exist_xyz");
  assert(src == nullptr);

  std::cout << "test_km_find_missing passed" << std::endl;
}

//
// test: list_loaded on empty library
//
static void test_km_list_loaded_empty() {
  KnowledgeManager km;
  std::string loaded = km.list_loaded();
  assert(loaded.empty() || loaded.find("none") != std::string::npos ||
         loaded.find("No") != std::string::npos);

  std::cout << "test_km_list_loaded_empty passed" << std::endl;
}

//
// test: status returns non-empty string
//
static void test_km_status() {
  KnowledgeManager km;
  km.load_meta("../../share/kiwix-meta.json");
  std::string s = km.status();
  assert(!s.empty());

  std::cout << "test_km_status passed" << std::endl;
}

//
// test: load and unload a .rag file
//
static void test_km_load_unload() {
  const std::string dir = "/tmp/hali_km_test_load";
  fs::remove_all(dir);
  fs::create_directories(dir);

  KnowledgeManager km;
  km.set_rag_dir(dir);
  km.set_zim_dir(dir + "/zim");

  /* Create a .rag file */
  make_rag_file(dir + "/testsource.rag", "testsource");

  /* Load it */
  assert(km.load("testsource"));
  assert(km.library().size() == 1);
  assert(km.library().get("testsource") != nullptr);
  assert(km.library().get("testsource")->size() == 1);

  /* Unload it */
  assert(km.unload("testsource"));
  assert(km.library().empty());

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_load_unload passed" << std::endl;
}

//
// test: load_all loads multiple .rag files
//
static void test_km_load_all() {
  const std::string dir = "/tmp/hali_km_test_loadall";
  fs::remove_all(dir);
  fs::create_directories(dir);

  KnowledgeManager km;
  km.set_rag_dir(dir);
  km.set_zim_dir(dir + "/zim");

  /* Create 3 .rag files */
  make_rag_file(dir + "/alpha.rag", "alpha");
  make_rag_file(dir + "/beta.rag", "beta");
  make_rag_file(dir + "/gamma.rag", "gamma");

  int n = km.load_all();
  assert(n == 3);
  assert(km.library().size() == 3);
  assert(km.library().get("alpha") != nullptr);
  assert(km.library().get("beta") != nullptr);
  assert(km.library().get("gamma") != nullptr);

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_load_all passed" << std::endl;
}

//
// test: remove deletes .rag and .zim files
//
static void test_km_remove() {
  const std::string dir = "/tmp/hali_km_test_remove";
  fs::remove_all(dir);
  fs::create_directories(dir + "/zim");

  KnowledgeManager km;
  km.set_rag_dir(dir);
  km.set_zim_dir(dir + "/zim");

  /* Create files */
  make_rag_file(dir + "/toremove.rag", "toremove");
  {
    std::ofstream ofs(dir + "/zim/toremove.zim");
    ofs << "fake zim data";
  }

  assert(fs::exists(dir + "/toremove.rag"));
  assert(fs::exists(dir + "/zim/toremove.zim"));

  /* Remove */
  assert(km.remove("toremove"));
  assert(!fs::exists(dir + "/toremove.rag"));
  assert(!fs::exists(dir + "/zim/toremove.zim"));

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_remove passed" << std::endl;
}

//
// test: rag_exists and zim_exists
//
static void test_km_exists() {
  const std::string dir = "/tmp/hali_km_test_exists";
  fs::remove_all(dir);
  fs::create_directories(dir + "/zim");

  KnowledgeManager km;
  km.set_rag_dir(dir);
  km.set_zim_dir(dir + "/zim");

  /* Nothing exists yet */
  assert(!km.rag_exists("foo"));
  assert(!km.zim_exists("foo"));

  /* Create a .rag file */
  make_rag_file(dir + "/foo.rag", "foo");
  assert(km.rag_exists("foo"));
  assert(!km.zim_exists("foo"));

  /* Create a .zim file */
  {
    std::ofstream ofs(dir + "/zim/foo.zim");
    ofs << "fake";
  }
  assert(km.rag_exists("foo"));
  assert(km.zim_exists("foo"));

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_exists passed" << std::endl;
}

void knowledge_test() {
  test_km_paths();
  test_km_load_meta();
  test_km_find();
  test_km_find_missing();
  test_km_list_loaded_empty();
  test_km_status();
  test_km_load_unload();
  test_km_load_all();
  test_km_remove();
  test_km_exists();
}