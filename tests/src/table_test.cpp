#include <iostream>
#include <cassert>
#include <string>
#include <sstream>
#include <vector>
#include "agent_utils.h"

using namespace std;

static string join_rows(const TableResult &r) {
  ostringstream oss;
  for (size_t i = 0; i < r.rows.size(); i++) {
    if (i > 0) oss << "\n";
    oss << r.rows[i];
  }
  return oss.str();
}

static string make_border(const string &L, const string &M, const string &R, const vector<int> &w) {
  string dash = "\u2500";
  string s = L;
  for (size_t i = 0; i < w.size(); i++) {
    for (int j = 0; j < w[i] + 2; j++) {
      s += dash;
    }
    s += (i < w.size() - 1) ? M : R;
  }
  return s;
}

static void test_basic_table() {
  std::string data = "name|age|city\nAlice|30|NYC\nBob|25|LA";
  TableResult result = tool_table("3x3", data);
  std::string out = join_rows(result);

  // Widths: name=5, age=3, city=4
  vector<int> w = {5, 3, 4};
  assert(out.find(make_border("\u250C", "\u252C", "\u2510", w)) != std::string::npos);
  assert(out.find("\u2502 name  \u2502 age \u2502 city \u2502") != std::string::npos);
  assert(out.find(make_border("\u251C", "\u253C", "\u2524", w)) != std::string::npos);
  assert(out.find("\u2502 Alice \u2502 30  \u2502 NYC  \u2502") != std::string::npos);
  assert(out.find("\u2502 Bob   \u2502 25  \u2502 LA   \u2502") != std::string::npos);
  assert(out.find(make_border("\u2514", "\u2534", "\u2518", w)) != std::string::npos);

  cout << "test_basic_table passed" << endl;
}

static void test_single_column() {
  std::string data = "item\nfoo\nbar";
  TableResult result = tool_table("3x1", data);
  std::string out = join_rows(result);

  vector<int> w = {4};
  assert(out.find(make_border("\u250C", "\u252C", "\u2510", w)) != std::string::npos);
  assert(out.find("\u2502 item \u2502") != std::string::npos);
  assert(out.find("\u2502 foo  \u2502") != std::string::npos);
  assert(out.find("\u2502 bar  \u2502") != std::string::npos);
  assert(out.find(make_border("\u2514", "\u2534", "\u2518", w)) != std::string::npos);

  cout << "test_single_column passed" << endl;
}

static void test_header_only() {
  std::string data = "a|b|c";
  TableResult result = tool_table("1x3", data);
  std::string out = join_rows(result);

  vector<int> w = {1, 1, 1};
  assert(out.find(make_border("\u250C", "\u252C", "\u2510", w)) != std::string::npos);
  assert(out.find("\u2502 a \u2502 b \u2502 c \u2502") != std::string::npos);
  assert(out.find(make_border("\u251C", "\u253C", "\u2524", w)) != std::string::npos);
  assert(out.find(make_border("\u2514", "\u2534", "\u2518", w)) != std::string::npos);

  cout << "test_header_only passed" << endl;
}

static void test_empty_cell() {
  std::string data = "name|age|city\nAlice||NYC\nBob|25|";
  TableResult result = tool_table("3x3", data);
  std::string out = join_rows(result);

  assert(out.find("\u2502 name  \u2502 age \u2502 city \u2502") != std::string::npos);
  assert(out.find("\u2502 Alice \u2502     \u2502 NYC  \u2502") != std::string::npos);
  assert(out.find("\u2502 Bob   \u2502 25  \u2502      \u2502") != std::string::npos);

  cout << "test_empty_cell passed" << endl;
}

static void test_bad_dims() {
  TableResult result = tool_table("abc", "data");
  assert(!result.error.empty());

  cout << "test_bad_dims passed" << endl;
}

static void test_empty_data() {
  TableResult result = tool_table("3x3", "");
  assert(!result.error.empty());

  cout << "test_empty_data passed" << endl;
}

static void test_wide_alignment() {
  std::string data = "short|a very long column header|med\n1|2|3";
  TableResult result = tool_table("2x3", data);
  std::string out = join_rows(result);

  assert(out.find("\u2502 short") != std::string::npos);
  assert(out.find("a very long column header") != std::string::npos);
  assert(out.find("\u2502 1     \u2502") != std::string::npos);

  cout << "test_wide_alignment passed" << endl;
}

static void test_multibyte_alignment() {
  std::string deg = "\xC2\xB0";
  std::string data = "Day|Max " + deg + "C|Min " + deg + "C|Precip\nMon|16.4|11.9|5.7";
  TableResult result = tool_table("2x4", data);
  std::string out = join_rows(result);

  // Widths: Day=3, Max °C=6, Min °C=6, Precip=6
  vector<int> w = {3, 6, 6, 6};
  assert(out.find(make_border("\u250C", "\u252C", "\u2510", w)) != std::string::npos);
  assert(out.find("\u2502 Day \u2502 Max " + deg + "C \u2502 Min " + deg + "C \u2502 Precip \u2502") != std::string::npos);
  assert(out.find("\u2502 Mon \u2502 16.4   \u2502 11.9   \u2502 5.7    \u2502") != std::string::npos);
  assert(out.find(make_border("\u2514", "\u2534", "\u2518", w)) != std::string::npos);

  cout << "test_multibyte_alignment passed" << endl;
}

void table_test() {
  cout << "=== Running table_test ===" << endl;
  test_basic_table();
  test_single_column();
  test_header_only();
  test_empty_cell();
  test_bad_dims();
  test_empty_data();
  test_wide_alignment();
  test_multibyte_alignment();
  cout << "All table tests passed!" << endl;
}