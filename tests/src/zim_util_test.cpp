// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

using namespace std;

/* extern declarations for de-static'd functions in zim.cpp */
extern std::string url_decode(const std::string &encoded);
extern bool is_redirect_stub(const std::string &content);
extern std::vector<std::string> load_filter_file(const std::string &path);
extern bool title_matches_filter(const std::string &filename,
                                 const std::vector<std::string> &keywords);

//
// test: url_decode basic percent-encoding
//
static void test_url_decode_basic() {
  assert(url_decode("hello") == "hello");
  assert(url_decode("hello%20world") == "hello world");
  assert(url_decode("100%25") == "100%");
  assert(url_decode("%41%42%43") == "ABC");
  assert(url_decode("a%2Db%2Fc") == "a-b/c");

  cout << "test_url_decode_basic passed" << endl;
}

//
// test: url_decode edge cases
//
static void test_url_decode_edge() {
  /* empty string */
  assert(url_decode("") == "");

  /* trailing incomplete percent (no two hex digits) */
  assert(url_decode("abc%") == "abc%");

  /* percent at second-to-last position */
  assert(url_decode("ab%2") == "ab%2");

  /* multiple consecutive encodings */
  assert(url_decode("%20%20%20") == "   ");

  /* non-ASCII byte */
  assert(url_decode("%C3%A9") == std::string("\xC3\xA9", 2));

  cout << "test_url_decode_edge passed" << endl;
}

//
// test: is_redirect_stub detects meta-refresh HTML
//
static void test_is_redirect_stub_positive() {
  string stub =
    "<html><head>"
    "<meta http-equiv=\"refresh\" content=\"0;url=A/Article.html\">"
    "</head><body></body></html>";
  assert(is_redirect_stub(stub));

  cout << "test_is_redirect_stub_positive passed" << endl;
}

//
// test: is_redirect_stub rejects normal content
//
static void test_is_redirect_stub_negative() {
  /* normal HTML page, no meta-refresh */
  string normal =
    "<html><head><title>Article</title></head>"
    "<body><p>This is a real article with content.</p></body></html>";
  assert(!is_redirect_stub(normal));

  /* empty string */
  assert(!is_redirect_stub(""));

  cout << "test_is_redirect_stub_negative passed" << endl;
}

//
// test: is_redirect_stub rejects large files (>2000 chars)
//
static void test_is_redirect_stub_too_large() {
  /* meta-refresh marker present but file is too large */
  string large(2500, 'x');
  large += "<meta http-equiv=\"refresh\" content=\"0;url=x\">";
  assert(!is_redirect_stub(large));

  cout << "test_is_redirect_stub_too_large passed" << endl;
}

//
// test: load_filter_file with a real temp file
//
static void test_load_filter_file() {
  const string path = "/tmp/hali_zim_filter_test.txt";

  {
    ofstream f(path);
    f << "# This is a comment\n";
    f << "\n";
    f << "health\n";
    f << "science, technology\n";
    f << "  \n";
    f << "history\n";
  }

  auto kws = load_filter_file(path);
  assert(kws.size() == 4);
  assert(kws[0] == "health");
  assert(kws[1] == "science");
  assert(kws[2] == "technology");
  assert(kws[3] == "history");

  filesystem::remove(path);
  cout << "test_load_filter_file passed" << endl;
}

//
// test: load_filter_file returns empty for missing file
//
static void test_load_filter_file_missing() {
  auto kws = load_filter_file("/tmp/does_not_exist_hali_filter.txt");
  assert(kws.empty());

  cout << "test_load_filter_file_missing passed" << endl;
}

//
// test: title_matches_filter basic matching
//
static void test_title_matches_basic() {
  vector<string> kws = {"health", "science"};

  /* exact word match */
  assert(title_matches_filter("Health.html", kws));

  /* case-insensitive */
  assert(title_matches_filter("HEALTH.html", kws));

  /* "healthcare" is a different word from "health" — no match */
  assert(!title_matches_filter("Healthcare.html", kws));

  /* underscore-delimited word match */
  assert(title_matches_filter("Public_health_systems.html", kws));

  /* no match */
  assert(!title_matches_filter("Cooking.html", kws));

  /* "unhealthy" does NOT match "health" (not a prefix of a word) */
  assert(!title_matches_filter("Unhealthy.html", kws));

  cout << "test_title_matches_basic passed" << endl;
}

//
// test: title_matches_filter with empty keywords
//
static void test_title_matches_empty_keywords() {
  vector<string> kws;
  assert(!title_matches_filter("Anything.html", kws));

  cout << "test_title_matches_empty_keywords passed" << endl;
}

//
// test: title_matches_filter strips extension
//
static void test_title_matches_extension() {
  vector<string> kws = {"article"};

  /* .html extension stripped before matching */
  assert(title_matches_filter("Article.html", kws));

  /* .txt extension also stripped */
  assert(title_matches_filter("Article.txt", kws));

  /* no extension */
  assert(title_matches_filter("Article", kws));

  cout << "test_title_matches_extension passed" << endl;
}

//
// test: title_matches_filter multi-word keywords
//
static void test_title_matches_multiword() {
  /* "house cat" should match "House_cat.html" */
  vector<string> kws = {"house cat"};
  assert(title_matches_filter("House_cat.html", kws));

  /* case-insensitive: "HOUSE CAT" matches "house_cat" */
  vector<string> kws2 = {"HOUSE CAT"};
  assert(title_matches_filter("house_cat.html", kws2));

  /* "cat breed" should NOT match "Cat_breeds.html" (exact: breed != breeds) */
  vector<string> kws3 = {"cat breed"};
  assert(!title_matches_filter("Cat_breeds.html", kws3));

  /* "wild cat" matches "Wild_cat.html" */
  vector<string> kws4 = {"wild cat"};
  assert(title_matches_filter("Wild_cat.html", kws4));

  /* "cat health" matches "Cat_health_and_nutrition.html" */
  vector<string> kws5 = {"cat health"};
  assert(title_matches_filter("Cat_health_and_nutrition.html", kws5));

  /* "cat health" should NOT match "Health_cat.html" (wrong order) */
  assert(!title_matches_filter("Health_cat.html", kws5));

  /* "cat health" should NOT match "cat.html" (missing second word) */
  assert(!title_matches_filter("Cat.html", kws5));

  /* "cat health" should NOT match "dog health.html" (first word mismatch) */
  assert(!title_matches_filter("Dog_health.html", kws5));

  /* three-word keyword: "cat domestic cat" matches "Cat_domestic_cat.html" */
  vector<string> kws6 = {"cat domestic cat"};
  assert(title_matches_filter("Cat_domestic_cat.html", kws6));

  /* three-word keyword should NOT match if words are not contiguous */
  vector<string> kws7 = {"cat wild cat"};
  assert(!title_matches_filter("Cat_domestic_cat.html", kws7));

  /* single-word keyword still works alongside multi-word */
  vector<string> kws8 = {"cat", "house cat"};
  assert(title_matches_filter("Cat.html", kws8));
  assert(title_matches_filter("House_cat.html", kws8));
  assert(!title_matches_filter("Dog.html", kws8));

  cout << "test_title_matches_multiword passed" << endl;
}

//
// test: title_matches_filter with real-world ZIM filename patterns
// Hyphens, colons, commas, parentheses, dots, asterisks, no extensions.
//
static void test_title_matches_real_filenames() {
  /* hyphen in filename: "half-life" keyword matches "Half-Life_2:_Episode_One" */
  vector<string> kws1 = {"half-life"};
  assert(title_matches_filter("Half-Life_2:_Episode_One", kws1));

  /* colon and hyphen: "episode one" matches "Half-Life_2:_Episode_One" */
  vector<string> kws2 = {"episode one"};
  assert(title_matches_filter("Half-Life_2:_Episode_One", kws2));

  /* parentheses: "railway station" matches "Broad_Street_railway_station_(England)" */
  vector<string> kws3 = {"railway station"};
  assert(title_matches_filter("Broad_Street_railway_station_(England)", kws3));

  /* asterisk and parens: "programming language" matches "F*_(programming_language)" */
  vector<string> kws4 = {"programming language"};
  assert(title_matches_filter("F*_(programming_language)", kws4));

  /* dots: "made in" matches "Made_in_the_A.M." */
  vector<string> kws5 = {"made in"};
  assert(title_matches_filter("Made_in_the_A.M.", kws5));

  /* comma: "nebraska" matches "Swanton,_Nebraska" */
  vector<string> kws6 = {"nebraska"};
  assert(title_matches_filter("Swanton,_Nebraska", kws6));

  /* no extension at all: "trade unions" matches "Trade_unions_in_India" */
  vector<string> kws7 = {"trade unions"};
  assert(title_matches_filter("Trade_unions_in_India", kws7));

  /* keyword with hyphen matches hyphenated filename word */
  vector<string> kws8 = {"half life"};
  assert(title_matches_filter("Half-Life_2:_Episode_One", kws8));

  /* keyword with hyphen also matches underscored filename */
  vector<string> kws9 = {"half-life"};
  assert(title_matches_filter("Half_Life_2", kws9));

  /* "customer premises" matches "Customer-premises_equipment" */
  vector<string> kws10 = {"customer premises"};
  assert(title_matches_filter("Customer-premises_equipment", kws10));

  /* numbers are preserved: "2010" matches "2010_Sibu_by-election" */
  vector<string> kws11 = {"2010"};
  assert(title_matches_filter("2010_Sibu_by-election", kws11));

  /* "ohio state" matches "2003_Ohio_State_Buckeyes_football_team" */
  vector<string> kws12 = {"ohio state"};
  assert(title_matches_filter("2003_Ohio_State_Buckeyes_football_team", kws12));

  /* negative: "state buckeyes" should NOT match "Ohio_State_Buckeyes"
     because "state" and "buckeyes" ARE contiguous, so it SHOULD match */
  vector<string> kws13 = {"state buckeyes"};
  assert(title_matches_filter("2003_Ohio_State_Buckeyes_football_team", kws13));

  /* negative: "ohio buckeyes" should NOT match (not contiguous) */
  vector<string> kws14 = {"ohio buckeyes"};
  assert(!title_matches_filter("2003_Ohio_State_Buckeyes_football_team", kws14));

  /* empty filename should not match */
  vector<string> kws15 = {"cat"};
  assert(!title_matches_filter("", kws15));

  /* punctuation-only filename should not match */
  assert(!title_matches_filter("___", kws15));

  cout << "test_title_matches_real_filenames passed" << endl;
}

void zim_util_test() {
  test_url_decode_basic();
  test_url_decode_edge();
  test_is_redirect_stub_positive();
  test_is_redirect_stub_negative();
  test_is_redirect_stub_too_large();
  test_load_filter_file();
  test_load_filter_file_missing();
  test_title_matches_basic();
  test_title_matches_empty_keywords();
  test_title_matches_extension();
  test_title_matches_multiword();
  test_title_matches_real_filenames();
}