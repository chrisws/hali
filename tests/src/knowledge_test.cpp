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
// test: rag_path generates correct path
//
static void test_km_paths() {
  KnowledgeManager km;
  km.set_rag_dir("/tmp/hali_km_test/rag");

  assert(km.rag_path("wikipedia") == "/tmp/hali_km_test/rag/wikipedia.rag");

  std::cout << "test_km_paths passed" << std::endl;
}

//
// test: load_index_json on missing file returns true (empty index)
//
static void test_km_load_index_missing() {
  KnowledgeManager km;
  km.set_index_json_path("/tmp/hali_km_test/nonexistent.json");
  bool ok = km.load_index_json();
  assert(ok);
  assert(km.indexed_ids().empty());

  std::cout << "test_km_load_index_missing passed" << std::endl;
}

//
// test: save_index_json and load_index_json roundtrip
//
static void test_km_index_json_roundtrip() {
  const std::string dir = "/tmp/hali_km_test_json";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string json_path = dir + "/kiwix-index.json";

  KnowledgeManager km;
  km.set_index_json_path(json_path);

  /* Empty index saves and loads fine */
  assert(km.save_index_json());
  assert(fs::exists(json_path));

  KnowledgeManager km2;
  km2.set_index_json_path(json_path);
  assert(km2.load_index_json());
  assert(km2.indexed_ids().empty());

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_index_json_roundtrip passed" << std::endl;
}

//
// test: find on empty index returns nullptr
//
static void test_km_find_missing() {
  KnowledgeManager km;
  const IndexedSource *src = km.find("this_id_does_not_exist_xyz");
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
  std::string s = km.status();
  assert(!s.empty());

  std::cout << "test_km_status passed" << std::endl;
}

//
// test: indexed_labels and indexed_ids on empty index
//
static void test_km_indexed_labels_empty() {
  KnowledgeManager km;
  assert(km.indexed_labels().empty());
  assert(km.indexed_ids().empty());

  std::cout << "test_km_indexed_labels_empty passed" << std::endl;
}

//
// test: loaded_labels and loaded_ids on empty library
//
static void test_km_loaded_labels_empty() {
  KnowledgeManager km;
  assert(km.loaded_labels().empty());
  assert(km.loaded_ids().empty());

  std::cout << "test_km_loaded_labels_empty passed" << std::endl;
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

  /* Create a .rag file */
  make_rag_file(dir + "/testsource.rag", "testsource");

  /* Load it */
  assert(km.load("testsource"));
  assert(km.library().size() == 1);
  assert(km.library().get("testsource") != nullptr);
  assert(km.library().get("testsource")->size() == 1);

  /* loaded_labels should reflect it */
  auto labels = km.loaded_labels();
  assert(labels.size() == 1);
  auto ids = km.loaded_ids();
  assert(ids.size() == 1);
  assert(ids[0] == "testsource");

  /* Unload it */
  assert(km.unload("testsource"));
  assert(km.library().empty());
  assert(km.loaded_labels().empty());

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

  /* loaded_labels should have 3 entries */
  assert(km.loaded_labels().size() == 3);
  assert(km.loaded_ids().size() == 3);

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_load_all passed" << std::endl;
}

//
// test: remove deletes .rag file
//
static void test_km_remove() {
  const std::string dir = "/tmp/hali_km_test_remove";
  fs::remove_all(dir);
  fs::create_directories(dir);

  KnowledgeManager km;
  km.set_rag_dir(dir);

  /* Create a .rag file */
  make_rag_file(dir + "/toremove.rag", "toremove");
  assert(fs::exists(dir + "/toremove.rag"));

  /* Remove */
  assert(km.remove("toremove"));
  assert(!fs::exists(dir + "/toremove.rag"));

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_remove passed" << std::endl;
}

//
// test: rag_exists
//
static void test_km_rag_exists() {
  const std::string dir = "/tmp/hali_km_test_exists";
  fs::remove_all(dir);
  fs::create_directories(dir);

  KnowledgeManager km;
  km.set_rag_dir(dir);

  /* Nothing exists yet */
  assert(!km.rag_exists("foo"));

  /* Create a .rag file */
  make_rag_file(dir + "/foo.rag", "foo");
  assert(km.rag_exists("foo"));

  /* Cleanup */
  fs::remove_all(dir);

  std::cout << "test_km_rag_exists passed" << std::endl;
}

//
// test: zimdump_available is callable (may be true or false)
//
static void test_km_zimdump_available() {
  /* Just verify it doesn't crash; result depends on system */
  (void)KnowledgeManager::zimdump_available();

  std::cout << "test_km_zimdump_available passed" << std::endl;
}

void knowledge_test() {
  test_km_paths();
  test_km_load_index_missing();
  test_km_index_json_roundtrip();
  test_km_find_missing();
  test_km_list_loaded_empty();
  test_km_status();
  test_km_indexed_labels_empty();
  test_km_loaded_labels_empty();
  test_km_load_unload();
  test_km_load_all();
  test_km_remove();
  test_km_rag_exists();
  test_km_zimdump_available();
}