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

#include "llama_sb_rag.h"

using namespace std;

static void test_ragdb_repair();
static void test_ragdb_repair_missing_file();
static void test_ragdb_repair_bad_magic();

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

  assert(lib.add("testdb", std::move(db)));
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

  lib.add("toremove", std::move(db));
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

  lib.add("roundtrip", std::move(db));

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
  lib.add("empty", std::move(empty_db));
  assert(lib.size() == 1);
  assert(lib.total_chunks() == 0);

  cout << "test_retrieve_multi_empty passed" << endl;
}

//
// test: RagDB incremental I/O: init_file, append_chunks, finalize_file
//
static void test_ragdb_incremental_io() {
  const string path = "/tmp/hali_rag_incr_test.rag";
  std::filesystem::remove(path);

  /* Phase 1: init_file writes header with n_chunks=0 */
  RagDB db;
  db.embed_dim = 4;
  assert(db.init_file(path));

  /* Verify file exists and has 16-byte header */
  assert(std::filesystem::exists(path));
  assert(std::filesystem::file_size(path) == 16);

  /* Phase 2: append first batch */
  RagChunk c1;
  c1.text = "first batch chunk with enough text to be valid";
  c1.source = "a.cpp";
  c1.type = "function";
  c1.embedding = {0.1f, 0.2f, 0.3f, 0.4f};
  db.chunks.push_back(c1);

  RagChunk c2;
  c2.text = "second batch chunk also has sufficient content";
  c2.source = "b.cpp";
  c2.type = "class";
  c2.embedding = {0.5f, 0.6f, 0.7f, 0.8f};
  db.chunks.push_back(c2);

  assert(db.append_chunks(path));
  /* chunks are cleared after append */
  assert(db.empty());

  /* Phase 3: append second batch */
  RagChunk c3;
  c3.text = "third chunk in the second batch of writes";
  c3.source = "c.cpp";
  c3.type = "other";
  c3.embedding = {0.9f, 0.1f, 0.2f, 0.3f};
  db.chunks.push_back(c3);

  assert(db.append_chunks(path));
  assert(db.empty());

  /* Phase 4: finalize patches n_chunks in header */
  assert(RagDB::finalize_file(path, 3));

  /* Phase 5: load and verify */
  RagDB loaded;
  assert(loaded.load(path));
  assert(loaded.size() == 3);
  assert(loaded.embed_dim == 4);

  assert(loaded.chunks[0].text == "first batch chunk with enough text to be valid");
  assert(loaded.chunks[0].source == "a.cpp");
  assert(loaded.chunks[0].type == "function");
  assert(loaded.chunks[1].text == "second batch chunk also has sufficient content");
  assert(loaded.chunks[1].source == "b.cpp");
  assert(loaded.chunks[2].text == "third chunk in the second batch of writes");
  assert(loaded.chunks[2].source == "c.cpp");

  /* Verify embeddings survived the round-trip */
  for (int i = 0; i < 4; i++) {
    assert(loaded.chunks[0].embedding[i] == c1.embedding[i]);
    assert(loaded.chunks[1].embedding[i] == c2.embedding[i]);
    assert(loaded.chunks[2].embedding[i] == c3.embedding[i]);
  }

  std::filesystem::remove(path);
  cout << "test_ragdb_incremental_io passed" << endl;
}

//
// test: RagDB append_chunks with empty chunks is a no-op
//
static void test_ragdb_append_empty() {
  const string path = "/tmp/hali_rag_empty_append.rag";
  std::filesystem::remove(path);

  RagDB db;
  db.embed_dim = 2;
  assert(db.init_file(path));

  /* appending zero chunks should succeed and not change file size */
  assert(db.append_chunks(path));
  assert(std::filesystem::file_size(path) == 16);

  std::filesystem::remove(path);
  cout << "test_ragdb_append_empty passed" << endl;
}

//
// test: RagDB init_file truncates existing file
//
static void test_ragdb_init_truncates() {
  const string path = "/tmp/hali_rag_trunc_test.rag";

  /* write some junk first */
  {
    std::ofstream f(path, std::ios::binary);
    f.write("JUNKJUNKJUNKJUNKJUNK", 20);
  }
  assert(std::filesystem::file_size(path) == 20);

  /* init_file should truncate to 16-byte header */
  RagDB db;
  db.embed_dim = 8;
  assert(db.init_file(path));
  assert(std::filesystem::file_size(path) == 16);

  std::filesystem::remove(path);
  cout << "test_ragdb_init_truncates passed" << endl;
}

//
// test: init_file writes correct "RAGD" magic and version header
// Regression: RAGINDEX was passing append=true, skipping init_file,
// producing .rag files with no magic header that failed to load.
//
static void test_ragdb_magic_header() {
  const string path = "/tmp/hali_rag_magic_test.rag";
  std::filesystem::remove(path);

  RagDB db;
  db.embed_dim = 4;
  assert(db.init_file(path));

  /* Read back the 16-byte header and verify fields */
  uint32_t magic = 0, version = 0, n_chunks = 0, dim = 0;
  {
    std::ifstream f(path, std::ios::binary);
    f.read((char*)&magic, 4);
    f.read((char*)&version, 4);
    f.read((char*)&n_chunks, 4);
    f.read((char*)&dim, 4);
  }

  assert(magic == 0x52414744u);   /* "RAGD" */
  assert(version == 2);
  assert(n_chunks == 0);
  assert(dim == 4);

  std::filesystem::remove(path);
  cout << "test_ragdb_magic_header passed" << endl;
}

//
// test: load() rejects a file with no magic header
// Simulates the corrupted-file scenario where init_file was skipped.
//
static void test_ragdb_load_rejects_no_magic() {
  const string path = "/tmp/hali_rag_no_magic.rag";
  std::filesystem::remove(path);

  /* Write a file that starts with raw text (no RAGD header) */
  {
    std::ofstream f(path, std::ios::binary);
    f.write("this is not a valid rag file at all", 34);
  }

  RagDB db;
  assert(!db.load(path));

  std::filesystem::remove(path);
  cout << "test_ragdb_load_rejects_no_magic passed" << endl;
}

//
// test: RagSession init sets up seen vector and token budget
//
static void test_session_init() {
  RagSession s;
  s.init(10, 4096);

  assert(s.seen.size() == 10);
  assert(s.tokens_used == 0);
  assert(s.tokens_max == 4096);

  /* all entries start un-seen */
  for (int i = 0; i < 10; i++) {
    assert(!s.is_seen(i));
  }

  cout << "test_session_init passed" << endl;
}

//
// test: RagSession reset clears seen flags and token count
//
static void test_session_reset() {
  RagSession s;
  s.init(5, 2048);

  s.mark(0);
  s.mark(2);
  s.mark(4);
  s.charge("hello world this is a test string");

  assert(s.is_seen(0));
  assert(s.is_seen(2));
  assert(s.is_seen(4));
  assert(s.tokens_used > 0);

  s.reset();

  assert(!s.is_seen(0));
  assert(!s.is_seen(2));
  assert(!s.is_seen(4));
  assert(s.tokens_used == 0);
  /* tokens_max is preserved */
  assert(s.tokens_max == 2048);

  cout << "test_session_reset passed" << endl;
}

//
// test: RagSession mark and is_seen boundary safety
//
static void test_session_mark_bounds() {
  RagSession s;
  s.init(3, 1024);

  /* valid indices */
  s.mark(0);
  s.mark(2);
  assert(s.is_seen(0));
  assert(s.is_seen(2));
  assert(!s.is_seen(1));

  /* out-of-range mark is a no-op (no crash) */
  s.mark(99);
  s.mark(-1);

  /* out-of-range is_seen returns false */
  assert(!s.is_seen(99));
  assert(!s.is_seen(-1));

  cout << "test_session_mark_bounds passed" << endl;
}

//
// test: RagSession budget_ok with zero max (unlimited)
//
static void test_session_budget_unlimited() {
  RagSession s;
  s.init(5, 0);  /* tokens_max = 0 means unlimited */

  /* any text should pass */
  assert(s.budget_ok(string(10000, 'x')));

  cout << "test_session_budget_unlimited passed" << endl;
}

//
// test: RagSession budget_ok with finite max
//
static void test_session_budget_finite() {
  RagSession s;
  s.init(5, 100);  /* 100 token budget */

  /* 400 chars / 4 = 100 tokens; threshold is 85% of 100 = 85 */
  /* so 400 chars would be 100 tokens, which is NOT < 85 */
  assert(!s.budget_ok(string(400, 'a')));

  /* 100 chars / 4 = 25 tokens; 25 < 85, so OK */
  assert(s.budget_ok(string(100, 'a')));

  /* after charging, budget shrinks */
  s.charge(string(300, 'a'));  /* 75 tokens used */
  /* 75 + 25 = 100, NOT < 85 */
  assert(!s.budget_ok(string(100, 'a')));

  /* 75 + 0 = 75 < 85, empty string is OK */
  assert(s.budget_ok(""));

  cout << "test_session_budget_finite passed" << endl;
}

//
// test: RagSession charge accumulates token estimates
//
static void test_session_charge() {
  RagSession s;
  s.init(5, 10000);

  assert(s.tokens_used == 0);

  s.charge(string(40, 'a'));   /* 40/4 = 10 tokens */
  assert(s.tokens_used == 10);

  s.charge(string(80, 'b'));   /* 80/4 = 20 tokens */
  assert(s.tokens_used == 30);

  s.charge(string(4, 'c'));    /* 4/4 = 1 token */
  assert(s.tokens_used == 31);

  cout << "test_session_charge passed" << endl;
}

//
// test: RagSession score_threshold default
//
static void test_session_score_threshold() {
  RagSession s;
  assert(s.score_threshold == 0.60f);

  s.score_threshold = 0.75f;
  assert(s.score_threshold == 0.75f);

  cout << "test_session_score_threshold passed" << endl;
}

//
// test: validate() passes on a healthy DB
//
static void test_ragdb_validate_ok() {
  RagDB db;
  db.embed_dim = 4;

  RagChunk c;
  c.text = "a valid chunk with enough text content";
  c.source = "ok.cpp";
  c.type = "function";
  c.embedding = {0.1f, 0.2f, 0.3f, 0.4f};
  db.chunks.push_back(c);

  auto issues = db.validate();
  assert(issues.empty());

  cout << "test_ragdb_validate_ok passed" << endl;
}

//
// test: validate() detects all-zero embeddings
// This is the suspected cause of the second-indexing bug:
// appended chunks have text but zeroed float vectors,
// so cosine similarity is always 0 and they never rank.
//
static void test_ragdb_validate_zero_embedding() {
  RagDB db;
  db.embed_dim = 4;

  /* good chunk */
  RagChunk good;
  good.text = "a good chunk with valid embedding data";
  good.source = "good.cpp";
  good.type = "function";
  good.embedding = {0.5f, 0.5f, 0.5f, 0.5f};
  db.chunks.push_back(good);

  /* zeroed chunk - the bug scenario */
  RagChunk bad;
  bad.text = "this chunk has text but zeroed embedding";
  bad.source = "bad.cpp";
  bad.type = "function";
  bad.embedding = {0.0f, 0.0f, 0.0f, 0.0f};
  db.chunks.push_back(bad);

  auto issues = db.validate();
  assert(issues.size() == 1);
  assert(issues[0].chunk_index == 1);
  assert(issues[0].reason.find("all-zero") != string::npos);

  cout << "test_ragdb_validate_zero_embedding passed" << endl;
}

//
// test: validate() detects empty text
//
static void test_ragdb_validate_empty_text() {
  RagDB db;
  db.embed_dim = 2;

  RagChunk c;
  c.text = "";
  c.source = "empty.cpp";
  c.type = "other";
  c.embedding = {0.1f, 0.2f};
  db.chunks.push_back(c);

  auto issues = db.validate();
  assert(issues.size() == 1);
  assert(issues[0].chunk_index == 0);
  assert(issues[0].reason == "empty text");

  cout << "test_ragdb_validate_empty_text passed" << endl;
}

//
// test: validate() detects embedding size mismatch
//
static void test_ragdb_validate_size_mismatch() {
  RagDB db;
  db.embed_dim = 4;

  RagChunk c;
  c.text = "chunk with wrong embedding dimension";
  c.source = "mismatch.cpp";
  c.type = "other";
  c.embedding = {0.1f, 0.2f};  /* only 2 floats, should be 4 */
  db.chunks.push_back(c);

  auto issues = db.validate();
  assert(issues.size() == 1);
  assert(issues[0].chunk_index == 0);
  assert(issues[0].reason.find("mismatch") != string::npos);

  cout << "test_ragdb_validate_size_mismatch passed" << endl;
}

//
// test: mmap round-trip with appended chunks (second-indexing scenario)
// This simulates the exact flow: init_file → append batch 1 →
// append batch 2 → finalize → load_mmap. Verifies that all
// chunks are readable with correct text AND non-zero embeddings.
//
static void test_ragdb_mmap_append_roundtrip() {
  const string path = "/tmp/hali_rag_mmap_test.rag";
  std::filesystem::remove(path);

  /* Phase 1: init */
  RagDB db;
  db.embed_dim = 4;
  assert(db.init_file(path));

  /* Phase 2: first batch (simulates initial indexing) */
  RagChunk c1;
  c1.text = "first batch: calico cat has orange and white patches";
  c1.source = "cats.md";
  c1.type = "prose";
  c1.embedding = {0.1f, 0.2f, 0.3f, 0.4f};
  db.chunks.push_back(c1);

  RagChunk c2;
  c2.text = "second entry in the first batch of indexed content";
  c2.source = "dogs.md";
  c2.type = "prose";
  c2.embedding = {0.5f, 0.6f, 0.7f, 0.8f};
  db.chunks.push_back(c2);

  assert(db.append_chunks(path));
  assert(db.empty());

  /* Phase 3: second batch (simulates RAGINDEX append) */
  RagChunk c3;
  c3.text = "third entry appended in the second indexing pass";
  c3.source = "birds.md";
  c3.type = "prose";
  c3.embedding = {0.9f, 0.1f, 0.2f, 0.3f};
  db.chunks.push_back(c3);

  RagChunk c4;
  c4.text = "fourth entry also appended during second pass";
  c4.source = "fish.md";
  c4.type = "prose";
  c4.embedding = {0.4f, 0.5f, 0.6f, 0.7f};
  db.chunks.push_back(c4);

  assert(db.append_chunks(path));
  assert(db.empty());

  /* Phase 4: finalize with total count */
  assert(RagDB::finalize_file(path, 4));

  /* Phase 5: load via mmap and verify */
  RagDB loaded;
  assert(loaded.load_mmap(path));
  assert(loaded.is_mmap());
  assert(loaded.size() == 4);
  assert(loaded.embed_dim == 4);

  /* Verify text survived */
  assert(loaded.chunks[0].text == "first batch: calico cat has orange and white patches");
  assert(loaded.chunks[1].text == "second entry in the first batch of indexed content");
  assert(loaded.chunks[2].text == "third entry appended in the second indexing pass");
  assert(loaded.chunks[3].text == "fourth entry also appended during second pass");

  /* Verify embeddings are non-zero and correct */
  for (int i = 0; i < 4; i++) {
    const float *emb = loaded.chunks[i].get_embedding();
    assert(emb != nullptr);
    for (int d = 0; d < 4; d++) {
      assert(emb[d] != 0.0f);
    }
  }

  /* Verify specific values */
  assert(loaded.chunks[0].get_embedding()[0] == 0.1f);
  assert(loaded.chunks[0].get_embedding()[3] == 0.4f);
  assert(loaded.chunks[2].get_embedding()[0] == 0.9f);
  assert(loaded.chunks[3].get_embedding()[1] == 0.5f);

  /* validate() should find no issues */
  auto issues = loaded.validate();
  assert(issues.empty());

  std::filesystem::remove(path);
  cout << "test_ragdb_mmap_append_roundtrip passed" << endl;
}

//
// test: mmap load detects zeroed embeddings via validate()
// Simulates the bug: text is present but embedding is all zeros.
//
static void test_ragdb_mmap_validate_zero() {
  const string path = "/tmp/hali_rag_mmap_zero.rag";
  std::filesystem::remove(path);

  RagDB db;
  db.embed_dim = 4;
  assert(db.init_file(path));

  /* good chunk */
  RagChunk good;
  good.text = "a valid chunk with proper embedding values";
  good.source = "ok.md";
  good.type = "prose";
  good.embedding = {0.3f, 0.4f, 0.5f, 0.6f};
  db.chunks.push_back(good);

  /* zeroed chunk - simulates the bug */
  RagChunk zeroed;
  zeroed.text = "this chunk has text but zeroed embedding vector";
  zeroed.source = "bug.md";
  zeroed.type = "prose";
  zeroed.embedding = {0.0f, 0.0f, 0.0f, 0.0f};
  db.chunks.push_back(zeroed);

  assert(db.append_chunks(path));
  assert(RagDB::finalize_file(path, 2));

  /* load via mmap */
  RagDB loaded;
  assert(loaded.load_mmap(path));
  assert(loaded.size() == 2);

  /* validate should flag chunk 1 */
  auto issues = loaded.validate();
  assert(issues.size() == 1);
  assert(issues[0].chunk_index == 1);
  assert(issues[0].reason.find("all-zero") != string::npos);

  std::filesystem::remove(path);
  cout << "test_ragdb_mmap_validate_zero passed" << endl;
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
  test_ragdb_incremental_io();
  test_ragdb_append_empty();
  test_ragdb_init_truncates();
  test_ragdb_magic_header();
  test_ragdb_load_rejects_no_magic();
  test_session_init();
  test_session_reset();
  test_session_mark_bounds();
  test_session_budget_unlimited();
  test_session_budget_finite();
  test_session_charge();
  test_session_score_threshold();
  test_ragdb_validate_ok();
  test_ragdb_validate_zero_embedding();
  test_ragdb_validate_empty_text();
  test_ragdb_validate_size_mismatch();
  test_ragdb_mmap_append_roundtrip();
  test_ragdb_mmap_validate_zero();
  test_ragdb_repair();
  test_ragdb_repair_missing_file();
  test_ragdb_repair_bad_magic();
}

//
// test: repair fixes a stale n_chunks header
//
static void test_ragdb_repair() {
  string path = "/tmp/hali_test_repair.rag";

  /* Create a file with 3 chunks */
  RagDB db;
  db.embed_dim = 4;
  db.init_file(path);

  for (int i = 0; i < 3; i++) {
    RagChunk c;
    c.text = "test chunk number " + to_string(i) +
             " with enough text to be valid";
    c.source = "test";
    c.type = "other";
    c.embedding.assign(4, 0.5f);
    db.chunks.push_back(std::move(c));
  }
  assert(db.append_chunks(path));
  db.chunks.clear();

  /* Now corrupt the header: set n_chunks to 1 */
  RagDB::finalize_file(path, 1);

  /* Verify the header is wrong */
  {
    RagDB check;
    assert(check.load(path));
    assert(check.size() == 1);  /* only reads 1 due to stale header */
  }

  /* Repair should find 3 actual chunks and fix the header */
  int count = RagDB::repair(path);
  assert(count == 3);

  /* Verify the file now loads all 3 chunks */
  {
    RagDB check;
    assert(check.load(path));
    assert(check.size() == 3);
  }

  std::filesystem::remove(path);
  cout << "test_ragdb_repair passed" << endl;
}

//
// test: repair returns -1 for missing file
//
static void test_ragdb_repair_missing_file() {
  int count = RagDB::repair("/tmp/hali_nonexistent_file.rag");
  assert(count == -1);
  cout << "test_ragdb_repair_missing_file passed" << endl;
}

//
// test: repair returns -1 for file with bad magic
//
static void test_ragdb_repair_bad_magic() {
  string path = "/tmp/hali_test_repair_badmagic.rag";
  {
    std::ofstream f(path, std::ios::binary);
    f.write("NOTMAGIC", 8);
    f.write("\x00\x00\x00\x00", 4);
    f.write("\x00\x00\x00\x00", 4);
  }
  int count = RagDB::repair(path);
  assert(count == -1);
  std::filesystem::remove(path);
  cout << "test_ragdb_repair_bad_magic passed" << endl;
}