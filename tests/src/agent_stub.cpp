// Test stubs for Agent methods
#include "agent.h"

std::string Agent::memory_info_text() const {
  return "";
}

void Agent::reset_conversation(const std::string &msg) {
  // no-op in tests
}

bool Agent::setup_model() {
  return false;
}

bool Agent::setup_embed(const std::string &path) {
  return false;
}

bool Agent::run_turn(const std::string &user_message) {
  return false;
}

bool Agent::run_turn_with_skill(const std::string &skill_text, const std::string &user_msg) {
  return false;
}

std::vector<std::string> Agent::tool_names() const {
  return {};
}

std::string Agent::invoke_tool_by_name(const std::string &name, const std::string &a1, const std::string &a2) {
  return "";
}

void Agent::notify(const std::string &msg) {
  // no-op in tests
}

bool Agent::rag_index(const std::string &path) const {
  return false;
}

bool Agent::rag_load_index(const std::string &path) const {
  return false;
}

void Agent::apply_generation_params() const {
  // no-op in tests
}

std::string Agent::rag_tool(const std::string &query) const {
  return "";
}

std::string Agent::restart() {
  return "";
}