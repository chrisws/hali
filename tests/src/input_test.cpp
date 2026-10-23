#include <iostream>
#include <cassert>
#include <string>
#include <sstream>
#include <filesystem>

// Expose private members for testing
#define private public
#include "input.cpp"
#undef private

using namespace std;

//
// Test: pos_of_word_start finds the beginning of the word at pos
//
static void test_pos_of_word_start() {
  Input in;

  in.input_buf_ = "hello world";
  assert(in.pos_of_word_start(0) == 0);
  assert(in.pos_of_word_start(2) == 0);
  assert(in.pos_of_word_start(4) == 0);
  assert(in.pos_of_word_start(6) == 6);
  assert(in.pos_of_word_start(8) == 6);
  assert(in.pos_of_word_start(10) == 6);

  in.input_buf_ = "  hello";
  assert(in.pos_of_word_start(2) == 2);
  assert(in.pos_of_word_start(4) == 2);

  in.input_buf_ = "hello";
  assert(in.pos_of_word_start(0) == 0);
  assert(in.pos_of_word_start(4) == 0);

  in.input_buf_ = "";
  assert(in.pos_of_word_start(0) == 0);

  cout << "test_pos_of_word_start passed" << endl;
}

//
// Test: pos_of_word_end finds the last character of the word at pos
//
static void test_pos_of_word_end() {
  Input in;

  in.input_buf_ = "hello world";
  assert(in.pos_of_word_end(0) == 4);
  assert(in.pos_of_word_end(2) == 4);
  assert(in.pos_of_word_end(4) == 4);
  assert(in.pos_of_word_end(6) == 10);
  assert(in.pos_of_word_end(8) == 10);
  assert(in.pos_of_word_end(10) == 10);

  in.input_buf_ = "hello";
  assert(in.pos_of_word_end(0) == 4);
  assert(in.pos_of_word_end(4) == 4);

  in.input_buf_ = "  hello";
  assert(in.pos_of_word_end(2) == 6);
  assert(in.pos_of_word_end(6) == 6);

  in.input_buf_ = "";
  assert(in.pos_of_word_end(0) == -1);

  cout << "test_pos_of_word_end passed" << endl;
}

//
// Test: move_to_prev_word navigates to the start of the previous word
//
static void test_move_to_prev_word() {
  Input in;

  in.input_buf_ = "hello world";
  // Middle of "hello" -> start of "hello"
  assert(in.move_to_prev_word(3) == 0);
  // Middle of "world" -> start of "world"
  assert(in.move_to_prev_word(8) == 6);
  // At start of buffer
  assert(in.move_to_prev_word(0) == 0);
  // At space -> start of previous word
  assert(in.move_to_prev_word(5) == 0);
  // At start of "world" (pos 6), skip space back to start of "hello"
  assert(in.move_to_prev_word(6) == 0);

  in.input_buf_ = "one two three";
  // o=0,n=1,e=2,' '=3,t=4,w=5,o=6,' '=7,t=8,h=9,r=10,e=11,e=12
  assert(in.move_to_prev_word(5) == 4);
  assert(in.move_to_prev_word(10) == 8);

  cout << "test_move_to_prev_word passed" << endl;
}

//
// Test: move_to_next_word navigates to the end of the next word
//
static void test_move_to_next_word() {
  Input in;

  in.input_buf_ = "hello world";
  // Start of "hello" -> position after "hello" (5)
  assert(in.move_to_next_word(0) == 5);
  // Middle of "hello" -> position after "hello"
  assert(in.move_to_next_word(2) == 5);
  // At space (pos 5), skip forward to end of "world"
  assert(in.move_to_next_word(5) == 11);
  // Start of "world" -> end of buffer (11)
  assert(in.move_to_next_word(6) == 11);
  // At end of buffer
  assert(in.move_to_next_word(11) == 11);

  in.input_buf_ = "one two three";
  assert(in.move_to_next_word(0) == 3);
  assert(in.move_to_next_word(4) == 7);
  assert(in.move_to_next_word(8) == 13);

  cout << "test_move_to_next_word passed" << endl;
}

//
// Test: delete_word_before removes the word before the cursor
//
static void test_delete_word_before() {
  Input in;

  in.input_buf_ = "hello world";
  int pos = in.delete_word_before(11);
  assert(in.input_buf_ == "hello ");
  assert(pos == 6);

  in.input_buf_ = "hello world";
  pos = in.delete_word_before(8);
  assert(in.input_buf_ == "hello rld");
  assert(pos == 6);

  in.input_buf_ = "hello";
  pos = in.delete_word_before(3);
  assert(in.input_buf_ == "lo");
  assert(pos == 0);

  in.input_buf_ = "hello";
  pos = in.delete_word_before(0);
  assert(in.input_buf_ == "hello");
  assert(pos == 0);

  cout << "test_delete_word_before passed" << endl;
}

//
// Test: kill_word_backward (same behavior as delete_word_before)
//
static void test_kill_word_backward() {
  Input in;

  in.input_buf_ = "hello world";
  int pos = in.kill_word_backward(11);
  assert(in.input_buf_ == "hello ");
  assert(pos == 6);

  in.input_buf_ = "hello";
  pos = in.kill_word_backward(4);
  assert(in.input_buf_ == "o");
  assert(pos == 0);

  cout << "test_kill_word_backward passed" << endl;
}

//
// Test: delete_word_at_cursor removes the word under the cursor
//
static void test_delete_word_at_cursor() {
  Input in;

  in.input_buf_ = "hello world";
  in.cursor_pos_ = 2;
  int pos = in.delete_word_at_cursor();
  assert(in.input_buf_ == " world");
  assert(pos == 0);

  in.input_buf_ = "hello world";
  in.cursor_pos_ = 7;
  pos = in.delete_word_at_cursor();
  assert(in.input_buf_ == "hello ");
  assert(pos == 6);

  in.input_buf_ = "hello";
  in.cursor_pos_ = 0;
  pos = in.delete_word_at_cursor();
  assert(in.input_buf_ == "");
  assert(pos == 0);

  cout << "test_delete_word_at_cursor passed" << endl;
}

//
// Test: uppercase_word converts the word under the cursor to uppercase
//
static void test_uppercase_word() {
  Input in;

  in.input_buf_ = "hello world";
  int pos = in.uppercase_word(2);
  assert(in.input_buf_ == "HELLO world");
  assert(pos == 0);

  in.input_buf_ = "hello world";
  pos = in.uppercase_word(7);
  assert(in.input_buf_ == "hello WORLD");
  assert(pos == 6);

  in.input_buf_ = "Hello World";
  pos = in.uppercase_word(1);
  assert(in.input_buf_ == "HELLO World");
  assert(pos == 0);

  // Non-alpha characters are left unchanged
  in.input_buf_ = "h3llo";
  pos = in.uppercase_word(2);
  assert(in.input_buf_ == "H3LLO");
  assert(pos == 0);

  cout << "test_uppercase_word passed" << endl;
}

//
// Test: lowercase_word converts the word under the cursor to lowercase
//
static void test_lowercase_word() {
  Input in;

  in.input_buf_ = "HELLO WORLD";
  int pos = in.lowercase_word(2);
  assert(in.input_buf_ == "hello WORLD");
  assert(pos == 0);

  in.input_buf_ = "HELLO WORLD";
  pos = in.lowercase_word(7);
  assert(in.input_buf_ == "HELLO world");
  assert(pos == 6);

  in.input_buf_ = "Hello World";
  pos = in.lowercase_word(1);
  assert(in.input_buf_ == "hello World");
  assert(pos == 0);

  // Non-alpha characters are left unchanged
  in.input_buf_ = "H3LLO";
  pos = in.lowercase_word(2);
  assert(in.input_buf_ == "h3llo");
  assert(pos == 0);

  cout << "test_lowercase_word passed" << endl;
}

//
// Test: word functions with underscores (is_word_char includes '_')
//
static void test_word_with_underscore() {
  Input in;

  in.input_buf_ = "foo_bar baz";
  // "foo_bar" is one word (underscore is a word char)
  assert(in.pos_of_word_start(3) == 0);
  assert(in.pos_of_word_end(3) == 6);

  in.input_buf_ = "foo_bar baz";
  assert(in.move_to_prev_word(4) == 0);
  assert(in.move_to_next_word(0) == 7);

  in.input_buf_ = "foo_bar baz";
  in.cursor_pos_ = 3;
  int pos = in.delete_word_at_cursor();
  assert(in.input_buf_ == " baz");
  assert(pos == 0);

  cout << "test_word_with_underscore passed" << endl;
}

//
// Test: word functions with empty buffer
//
static void test_empty_buffer() {
  Input in;

  in.input_buf_ = "";
  assert(in.pos_of_word_start(0) == 0);
  assert(in.pos_of_word_end(0) == -1);
  assert(in.move_to_prev_word(0) == 0);
  assert(in.move_to_next_word(0) == 0);

  cout << "test_empty_buffer passed" << endl;
}

//
// Test: word navigation with multiple spaces between words
//
static void test_multiple_spaces() {
  Input in;

  // "position         in    multiple      spaces"
  //  p(0) o(1) s(2) i(3) t(4) i(5) o(6) n(7) sp(8-16) i(17) n(18)
  //  sp(19-22) m(23) u(24) l(25) t(26) i(27) p(28) l(29) e(30)
  //  sp(31-36) s(37) p(38) a(39) c(40) e(41) s(42)
  in.input_buf_ = "position         in    multiple      spaces";

  // move_to_prev_word at word starts
  assert(in.move_to_prev_word(17) == 0);   // start of "in" -> start of "position"
  assert(in.move_to_prev_word(23) == 17);  // start of "multiple" -> start of "in"
  assert(in.move_to_prev_word(37) == 23);  // start of "spaces" -> start of "multiple"

  // move_to_next_word at spaces
  assert(in.move_to_next_word(8) == 19);   // space after "position" -> end of "in"
  assert(in.move_to_next_word(19) == 31);  // space after "in" -> end of "multiple"
  assert(in.move_to_next_word(31) == 43);  // space after "multiple" -> end of "spaces"

  // delete_word_before at various positions
  in.input_buf_ = "position         in    multiple      spaces";
  int pos = in.delete_word_before(17);
  assert(in.input_buf_ == "in    multiple      spaces");
  assert(pos == 0);

  in.input_buf_ = "position         in    multiple      spaces";
  pos = in.delete_word_before(37);
  assert(in.input_buf_ == "position         in    spaces");
  assert(pos == 23);

  cout << "test_multiple_spaces passed" << endl;
}

//
// Test: update_completion with no slash prefix → no completion
//
static void test_completion_no_slash() {
  Input in;

  in.input_buf_ = "hello";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  in.input_buf_ = "";
  in.update_completion();
  assert(!in.completion_active_);

  in.input_buf_ = "  /not at start";
  in.update_completion();
  assert(!in.completion_active_);

  cout << "test_completion_no_slash passed" << endl;
}

//
// Test: update_completion with "/" alone → multiple matches → no completion
//
static void test_completion_slash_only() {
  Input in;

  in.input_buf_ = "/";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  cout << "test_completion_slash_only passed" << endl;
}

//
// Test: update_completion single match shows suffix
//
static void test_completion_single_match() {
  Input in;

  // /mo → /model → suffix "del"
  in.input_buf_ = "/mo";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "del");

  // /mod → /model → suffix "el"
  in.input_buf_ = "/mod";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "el");

  // /h → /help → suffix "elp"
  in.input_buf_ = "/h";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "elp");

  // /c → /clear → suffix "lear"
  in.input_buf_ = "/c";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "lear");

  // /th → /theme → suffix "eme"
  in.input_buf_ = "/th";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "eme");

  // /to → /tool → suffix "ol"
  in.input_buf_ = "/to";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "ol");

  // /e → /embed → suffix "mbed"
  in.input_buf_ = "/e";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "mbed");

  // /ra → /rag → suffix "g"
  in.input_buf_ = "/ra";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "g");

  // /re → /revoke → suffix "voke"
  in.input_buf_ = "/re";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "voke");

  // /mem → /memory → suffix "ory"
  in.input_buf_ = "/mem";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "ory");

  cout << "test_completion_single_match passed" << endl;
}

//
// Test: update_completion exact match → no suffix (nothing left to suggest)
//
static void test_completion_exact_match() {
  Input in;

  in.input_buf_ = "/model";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  in.input_buf_ = "/help";
  in.update_completion();
  assert(!in.completion_active_);

  in.input_buf_ = "/clear";
  in.update_completion();
  assert(!in.completion_active_);

  cout << "test_completion_exact_match passed" << endl;
}

//
// Test: update_completion multiple matches → no completion
//
static void test_completion_multiple_matches() {
  Input in;

  // /m → /model, /memory → no completion
  in.input_buf_ = "/m";
  in.update_completion();
  assert(!in.completion_active_);

  // /s → /session, /set, /settings, /save → no completion
  in.input_buf_ = "/s";
  in.update_completion();
  assert(!in.completion_active_);

  // /se → /session, /set, /settings → no completion
  in.input_buf_ = "/se";
  in.update_completion();
  assert(!in.completion_active_);

  // /a → /audio, /allow, /audit → no completion
  in.input_buf_ = "/a";
  in.update_completion();
  assert(!in.completion_active_);

  // /au → /audio, /audit → common prefix "/audi" → suffix "di"
  in.input_buf_ = "/au";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "di");

  // /r → /rag, /revoke → no completion
  in.input_buf_ = "/r";
  in.update_completion();
  assert(!in.completion_active_);

  cout << "test_completion_multiple_matches passed" << endl;
}

//
// Test: update_completion shows common prefix when multiple matches share it
//
static void test_completion_common_prefix() {
  Input in;

  // /al → matches /allow, /allow-list, /allow-remove
  // common prefix is "/allow" → suffix "low"
  in.input_buf_ = "/al";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "low");

  // /all → same matches → common prefix "/allow" → suffix "ow"
  in.input_buf_ = "/all";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "ow");

  // /allo → same matches → common prefix "/allow" → suffix "w"
  in.input_buf_ = "/allo";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "w");

  // /allow → same matches → common prefix "/allow" → equals buffer → no completion
  in.input_buf_ = "/allow";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  // /allow- → matches /allow-list, /allow-remove
  // common prefix is "/allow-" → equals buffer → no completion
  in.input_buf_ = "/allow-";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  // /allow-l → single match /allow-list → suffix "ist"
  in.input_buf_ = "/allow-l";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "ist");

  // /allow-r → single match /allow-remove → suffix "emove"
  in.input_buf_ = "/allow-r";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "emove");

  // /allow-li → single match /allow-list → suffix "st"
  in.input_buf_ = "/allow-li";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "st");

  // /allow-re → single match /allow-remove → suffix "move"
  in.input_buf_ = "/allow-re";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "move");

  // /a → matches /audio, /allow, /allow-list, /allow-remove, /audit
  // common prefix is "/a" → equals buffer → no completion
  in.input_buf_ = "/a";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  // /au → matches /audio, /audit
  // common prefix is "/audi" → suffix "di"
  in.input_buf_ = "/au";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "di");

  // /aud → matches /audio, /audit
  // common prefix is "/audi" → suffix "i"
  in.input_buf_ = "/aud";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "i");

  // /audi → matches /audio, /audit
  // common prefix is "/audi" → equals buffer → no completion
  in.input_buf_ = "/audi";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  // /audio → exact match → no suffix
  in.input_buf_ = "/audio";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  // /audit → exact match → no suffix
  in.input_buf_ = "/audit";
  in.update_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  cout << "test_completion_common_prefix passed" << endl;
}

//
// Test: update_completion narrows from multiple to single
//
static void test_completion_narrowing() {
  Input in;

  // /se → /session, /set, /settings → multiple → no completion
  in.input_buf_ = "/se";
  in.update_completion();
  assert(!in.completion_active_);

  // /ses → /session only → suffix "sion"
  in.input_buf_ = "/ses";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "sion");

  // /sess → /session only → suffix "ion"
  in.input_buf_ = "/sess";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "ion");

  // /s → /session, /set, /settings, /save → multiple
  in.input_buf_ = "/s";
  in.update_completion();
  assert(!in.completion_active_);

  // /sa → /save only → suffix "ve"
  in.input_buf_ = "/sa";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "ve");

  cout << "test_completion_narrowing passed" << endl;
}

//
// Test: update_completion is case-insensitive
//
static void test_completion_case_insensitive() {
  Input in;

  in.input_buf_ = "/MO";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "del");

  in.input_buf_ = "/MOD";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "el");

  in.input_buf_ = "/HELP";
  in.update_completion();
  assert(!in.completion_active_);  // exact match, no suffix

  in.input_buf_ = "/hElp";
  in.update_completion();
  assert(!in.completion_active_);  // exact match (case-insensitive)

  cout << "test_completion_case_insensitive passed" << endl;
}

//
// Test: dismiss_completion clears state
//
static void test_dismiss_completion() {
  Input in;

  in.input_buf_ = "/mo";
  in.update_completion();
  assert(in.completion_active_);
  assert(in.completion_suffix_ == "del");

  in.dismiss_completion();
  assert(!in.completion_active_);
  assert(in.completion_suffix_.empty());

  cout << "test_dismiss_completion passed" << endl;
}

//
// Test: update_completion dismisses when buffer no longer matches
//
static void test_completion_dismiss_on_edit() {
  Input in;

  in.input_buf_ = "/mo";
  in.update_completion();
  assert(in.completion_active_);

  // User types a space → no longer a valid prefix
  in.input_buf_ = "/mo ";
  in.update_completion();
  assert(!in.completion_active_);

  // User backspaces the slash
  in.input_buf_ = "m";
  in.update_completion();
  assert(!in.completion_active_);

  cout << "test_completion_dismiss_on_edit passed" << endl;
}

void input_test() {
  test_pos_of_word_start();
  test_pos_of_word_end();
  test_move_to_prev_word();
  test_move_to_next_word();
  test_delete_word_before();
  test_kill_word_backward();
  test_delete_word_at_cursor();
  test_uppercase_word();
  test_lowercase_word();
  test_word_with_underscore();
  test_empty_buffer();
  test_multiple_spaces();
  test_completion_no_slash();
  test_completion_slash_only();
  test_completion_single_match();
  test_completion_exact_match();
  test_completion_multiple_matches();
  test_completion_common_prefix();
  test_completion_narrowing();
  test_completion_case_insensitive();
  test_dismiss_completion();
  test_completion_dismiss_on_edit();

  cout << "\nAll input tests passed!\n" << endl;
}