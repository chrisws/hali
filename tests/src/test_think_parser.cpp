#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include "agent_utils.h"

// Build tag strings via concatenation to avoid harness interference
static std::string T_OPEN()  { return "<" "think" ">"; }
static std::string T_CLOSE() { return "</" "think" ">"; }
static std::string T_OPEN2()  { return "<|" "think" "|>"; }
static std::string T_CLOSE2() { return "</|" "think" "|>"; }
static std::string T_OPEN3()  { return "<" "think" "|>"; }
static std::string T_CLOSE3() { return "</" "think" "|>"; }
static std::string T_OPEN4()  { return "<|" "channel" ">thought"; }
static std::string T_CLOSE4() { return "</" "thought" ">"; }

void think_parser_test() {
  std::cout << "=== ThinkParser Tests ===" << std::endl;

  // --- assemble_tag ---
  {
    std::vector<std::string> tokens = {"<", "t", "h", "i", "n", "k", ">"};
    assert(ThinkParser::assemble_tag(tokens) == T_OPEN());

    tokens = {"<", "|", "t", "h", "i", "n", "k", "|", ">"};
    assert(ThinkParser::assemble_tag(tokens) == T_OPEN2());

    assert(ThinkParser::assemble_tag({}).empty());

    tokens = {"a", "b", "c"};
    assert(ThinkParser::assemble_tag(tokens).empty());

    tokens.clear();
    tokens.push_back("<");
    for (int i = 0; i < 40; i++) { tokens.push_back("a"); }
    auto result = ThinkParser::assemble_tag(tokens);
    assert(result.size() <= 32);
    assert(result.find('>') == std::string::npos);

    tokens = {"<", "a", "b", ">", "c", "d", ">"};
    assert(ThinkParser::assemble_tag(tokens) == "<ab>");
  }
  std::cout << "  assemble_tag: OK" << std::endl;

  // --- check_start ---
  {
    ThinkParser tp(true);
    assert(tp.mode() == ThinkParser::Init);

    assert(tp.check_start(T_OPEN()) == T_OPEN().size());
    assert(tp.mode() == ThinkParser::Think);

    tp.set_mode(ThinkParser::Init);
    assert(tp.check_start(T_OPEN2()) == T_OPEN2().size());
    assert(tp.mode() == ThinkParser::Think);

    tp.set_mode(ThinkParser::Init);
    assert(tp.check_start(T_OPEN3()) == T_OPEN3().size());
    assert(tp.mode() == ThinkParser::Think);

    tp.set_mode(ThinkParser::Init);
    assert(tp.check_start(T_OPEN4()) == T_OPEN4().size());
    assert(tp.mode() == ThinkParser::Think);

    // No match
    tp.set_mode(ThinkParser::Init);
    assert(tp.check_start("hello world") == 0);
    assert(tp.mode() == ThinkParser::Init);

    // Already in Think — no transition
    tp.set_mode(ThinkParser::Think);
    assert(tp.check_start(T_OPEN()) == 0);
  }
  std::cout << "  check_start: OK" << std::endl;

  // --- check_end ---
  {
    ThinkParser tp(true);
    tp.set_mode(ThinkParser::Think);

    // Standard close tag
    auto r = tp.check_end("some thought " + T_CLOSE());
    assert(r.found);
    assert(r.thought == "some thought ");
    assert(r.tag_len == T_CLOSE().size());
    assert(tp.mode() == ThinkParser::Thunk);

    // Pipe variant
    tp.set_mode(ThinkParser::Think);
    r = tp.check_end("data " + T_CLOSE2());
    assert(r.found);
    assert(r.thought == "data ");
    assert(r.tag_len == T_CLOSE2().size());

    // Alt pipe variant
    tp.set_mode(ThinkParser::Think);
    r = tp.check_end("x" + T_CLOSE3());
    assert(r.found);
    assert(r.thought == "x");
    assert(r.tag_len == T_CLOSE3().size());

    // Channel thought variant
    tp.set_mode(ThinkParser::Think);
    r = tp.check_end("y" + T_CLOSE4());
    assert(r.found);
    assert(r.thought == "y");
    assert(r.tag_len == T_CLOSE4().size());

    // No close tag found
    tp.set_mode(ThinkParser::Think);
    r = tp.check_end("just some text");
    assert(!r.found);
    assert(tp.mode() == ThinkParser::Think);

    // Empty thought
    tp.set_mode(ThinkParser::Think);
    r = tp.check_end(T_CLOSE());
    assert(r.found);
    assert(r.thought.empty());
  }
  std::cout << "  check_end: OK" << std::endl;

  // --- gemma_4 clear_buffer ---
  {
    ThinkParser tp(true, true);
    assert(tp.mode() == ThinkParser::Init);

    tp.set_mode(ThinkParser::Think);
    auto r = tp.check_end("gemma thought " + T_CLOSE());
    assert(r.found);
    assert(r.clear_buffer);
    assert(r.thought == "gemma thought ");
    assert(tp.mode() == ThinkParser::Thunk);
  }
  std::cout << "  gemma_4 clear_buffer: OK" << std::endl;

  // --- State transitions ---
  {
    // Disabled starts at Thunk
    ThinkParser tp(false);
    assert(tp.mode() == ThinkParser::Thunk);

    // reset()
    ThinkParser tp2(true);
    tp2.set_mode(ThinkParser::Think);
    tp2.reset();
    assert(tp2.mode() == ThinkParser::Init);

    ThinkParser tp3(true, true);
    tp3.set_mode(ThinkParser::Think);
    tp3.reset();
    assert(tp3.mode() == ThinkParser::Thunk);
  }
  std::cout << "  state transitions: OK" << std::endl;

  std::cout << "=== ThinkParser: ALL PASSED ===" << std::endl;
}
