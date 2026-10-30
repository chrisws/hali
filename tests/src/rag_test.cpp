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
#include <string>
#include <vector>

#include "llama_sb_rag.h"

using namespace std;

//
// test: empty input produces no chunks
//
static void test_prose_empty() {
  auto chunks = prose_chunk_split("");
  assert(chunks.empty());

  auto chunks2 = prose_chunk_split("   \n\n  \n\n  ");
  assert(chunks2.empty());

  cout << "test_prose_empty passed" << endl;
}

//
// test: short text below minimum produces no chunks
//
static void test_prose_too_short() {
  /* 30 chars - below PROSE_MIN of 80 */
  auto chunks = prose_chunk_split("This is a very short paragraph.");
  assert(chunks.empty());

  cout << "test_prose_too_short passed" << endl;
}

//
// test: single paragraph above minimum produces one chunk
//
static void test_prose_single_paragraph() {
  /* ~100 chars, one paragraph */
  string text = "Water purification is the process of removing unwanted chemical or "
                "biological contaminants from water.";
  auto chunks = prose_chunk_split(text);
  assert(chunks.size() == 1);
  assert(chunks[0].size() >= 80);
  assert(chunks[0].find("Water purification") != string::npos);

  cout << "test_prose_single_paragraph passed" << endl;
}

//
// test: multiple paragraphs accumulate into one chunk if under target
//
static void test_prose_accumulate() {
  /* two short paragraphs, total < 800 */
  string text =
    "First paragraph about survival skills in the wilderness.\n\n"
    "Second paragraph about building shelter from natural materials.";
  auto chunks = prose_chunk_split(text);
  assert(chunks.size() == 1);
  assert(chunks[0].find("First paragraph") != string::npos);
  assert(chunks[0].find("Second paragraph") != string::npos);

  cout << "test_prose_accumulate passed" << endl;
}

//
// test: many paragraphs split into multiple chunks at ~800 char boundary
//
static void test_prose_multi_chunk() {
  /* build text with 20 paragraphs of ~80 chars each = ~1600 chars total */
  string text;
  for (int i = 0; i < 20; i++) {
    text += "Paragraph number " + to_string(i) +
            " contains some meaningful content about a particular topic "
            "that requires further explanation and detail.\n\n";
  }
  auto chunks = prose_chunk_split(text);
  assert(chunks.size() >= 2);
  /* total content preserved (rough check) */
  size_t total = 0;
  for (const auto &c : chunks) total += c.size();
  assert(total > 1000);

  cout << "test_prose_multi_chunk passed" << endl;
}

//
// test: very long paragraph gets sentence-split
//
static void test_prose_long_paragraph() {
  /* build a 2000-char paragraph with sentence boundaries */
  string text;
  for (int i = 0; i < 20; i++) {
    text += "This is sentence number " + to_string(i) +
            " with enough content to be meaningful. ";
  }
  /* no double newlines - single paragraph */
  assert(text.find("\n\n") == string::npos);
  assert(text.size() > 1200);

  auto chunks = prose_chunk_split(text);
  assert(chunks.size() >= 2);
  for (const auto &c : chunks) {
    assert(c.size() >= 80);
  }

  cout << "test_prose_long_paragraph passed" << endl;
}

//
// test: single newlines do not split paragraphs
//
static void test_prose_single_newline() {
  /* single newlines should NOT create paragraph breaks */
  string text =
    "Line one of a continuous paragraph.\n"
    "Line two continues the same thought.\n"
    "Line three adds more detail to the discussion.\n"
    "Line four wraps up the point being made here.";
  auto chunks = prose_chunk_split(text);
  /* should be one chunk (or zero if too short) */
  if (!chunks.empty()) {
    assert(chunks.size() == 1);
  }

  cout << "test_prose_single_newline passed" << endl;
}

//
// test: chunk sizes stay within reasonable bounds
//
static void test_prose_size_bounds() {
  /* build ~5000 chars of text */
  string text;
  for (int i = 0; i < 50; i++) {
    text += "Sentence " + to_string(i) +
            " provides information about the subject at hand.\n\n";
  }
  auto chunks = prose_chunk_split(text);
  assert(!chunks.empty());
  for (const auto &c : chunks) {
    assert(c.size() >= 80);       /* above minimum */
    assert(c.size() <= 2000);     /* well below absurd */
  }

  cout << "test_prose_size_bounds passed" << endl;
}

//
// test: new library is empty
//
static void test_library_empty() {
  RagLibrary lib;
  assert(lib.empty());
  assert(lib.size() == 0);
  assert(lib.total_chunks() == 0);
  assert(lib.names().empty());

  cout << "test_library_empty passed" << endl;
}

//
// test: add and get a DB
//
static void test_library_add_get() {
  RagLibrary lib;

  RagDB db;
  db.embed_dim = 4;
  RagChunk c;
  c.text = "hello world this is a test chunk";
  c.source = "test.cpp";
  c.type = "function";
  c.embedding = {0.1f, 0.2f, 0.3f, 0.4f};
  db.chunks.push_back(c);

  assert(lib.add("testdb", db));
  assert(lib.size() == 1);
  assert(!lib.empty());
  assert(lib.total_chunks() == 1);

  RagDB *got = lib.get("testdb");
  assert(got != nullptr);
  assert(got->size() == 1);
  assert(got->chunks[0].text == "hello world this is a test chunk");

  /* non-existent name returns nullptr */
  assert(lib.get("nope") == nullptr);

  cout << "test_library_add_get passed" << endl;
}

//
// test: remove a DB
//
static void test_library_remove() {
  RagLibrary lib;

  RagDB db;
  db.embed_dim = 2;
  RagChunk c;
  c.text = "some test content here for removal";
  c.source = "a.cpp";
  c.type = "other";
  c.embedding = {0.5f, 0.5f};
  db.chunks.push_back(c);

  lib.add("toremove", db);
  assert(lib.size() == 1);

  assert(lib.remove("toremove"));
  assert(lib.empty());
  assert(lib.size() == 0);

  /* removing non-existent returns false */
  assert(!lib.remove("toremove"));

  cout << "test_library_remove passed" << endl;
}

//
// test: names() returns sorted list
//
static void test_library_names() {
  RagLibrary lib;

  auto make_db = [](int dim) {
    RagDB db;
    db.embed_dim = dim;
    RagChunk c;
    c.text = "content for name test purposes here";
    c.source = "x.cpp";
    c.type = "other";
    c.embedding.assign(dim, 0.1f);
    db.chunks.push_back(c);
    return db;
  };

  lib.add("zeta", make_db(2));
  lib.add("alpha", make_db(2));
  lib.add("mid", make_db(2));

  auto ns = lib.names();
  assert(ns.size() == 3);
  assert(ns[0] == "alpha");
  assert(ns[1] == "mid");
  assert(ns[2] == "zeta");

  cout << "test_library_names passed" << endl;
}

//
// test: total_chunks sums across all DBs
//
static void test_library_total_chunks() {
  RagLibrary lib;

  auto make_db = [](int n) {
    RagDB db;
    db.embed_dim = 2;
    for (int i = 0; i < n; i++) {
      RagChunk c;
      c.text = "chunk " + to_string(i) + " with enough text";
      c.source = "f.cpp";
      c.type = "other";
      c.embedding = {0.1f, 0.2f};
      db.chunks.push_back(c);
    }
    return db;
  };

  lib.add("a", make_db(3));
  lib.add("b", make_db(5));

  assert(lib.total_chunks() == 8);

  cout << "test_library_total_chunks passed" << endl;
}

//
// test: save_dir and load_dir round-trip
//
static void test_library_save_load_dir() {
  RagLibrary lib;

  RagDB db;
  db.embed_dim = 3;
  RagChunk c;
  c.text = "round trip test content for save load";
  c.source = "rt.cpp";
  c.type = "function";
  c.embedding = {0.1f, 0.2f, 0.3f};
  db.chunks.push_back(c);

  lib.add("roundtrip", db);

  const string dir = "/tmp/hali_rag_test";
  /* clean up from previous runs */
  std::filesystem::remove_all(dir);

  assert(lib.save_dir(dir));

  /* load into a fresh library */
  RagLibrary lib2;
  int n = lib2.load_dir(dir);
  assert(n == 1);
  assert(lib2.size() == 1);

  RagDB *got = lib2.get("roundtrip");
  assert(got != nullptr);
  assert(got->size() == 1);
  assert(got->chunks[0].text == "round trip test content for save load");
  assert(got->chunks[0].source == "rt.cpp");
  assert(got->chunks[0].type == "function");
  assert(got->embed_dim == 3);

  /* cleanup */
  std::filesystem::remove_all(dir);

  cout << "test_library_save_load_dir passed" << endl;
}

//
// test: load_dir on non-existent directory returns 0
//
static void test_library_load_dir_missing() {
  RagLibrary lib;
  int n = lib.load_dir("/tmp/does_not_exist_hali_test");
  assert(n == 0);
  assert(lib.empty());

  cout << "test_library_load_dir_missing passed" << endl;
}

//
// test: rag_retrieve_multi with empty library returns empty
//
static void test_retrieve_multi_empty() {
  /* We can't call rag_retrieve_multi without a Llama instance,
     but we can verify the RagLibrary edge cases it depends on. */
  RagLibrary lib;
  assert(lib.empty());
  assert(lib.total_chunks() == 0);

  /* library with only empty DBs */
  RagDB empty_db;
  lib.add("empty", empty_db);
  assert(lib.size() == 1);
  assert(lib.total_chunks() == 0);

  cout << "test_retrieve_multi_empty passed" << endl;
}

void rag_test() {
  test_prose_empty();
  test_prose_too_short();
  test_prose_single_paragraph();
  test_prose_accumulate();
  test_prose_multi_chunk();
  test_prose_long_paragraph();
  test_prose_single_newline();
  test_prose_size_bounds();
  test_library_empty();
  test_library_add_get();
  test_library_remove();
  test_library_names();
  test_library_total_chunks();
  test_library_save_load_dir();
  test_library_load_dir_missing();
  test_retrieve_multi_empty();
}