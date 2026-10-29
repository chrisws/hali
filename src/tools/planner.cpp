// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <sstream>
#include <string>
#include <vector>

#include "planner.h"

void Planner::set_goal(const std::string &goal) {
  goal_ = goal;
}

void Planner::add_step(const std::string &description) {
  PlanStep s;
  s.description = description;
  // If this is the first step, mark it Active
  if (steps_.empty()) {
    s.status = PlanStep::Active;
    current_ = 0;
  }
  steps_.push_back(s);
}

std::vector<PlanStep> Planner::steps() const {
  return steps_;
}

void Planner::advance() {
  if (current_ >= steps_.size()) {
    return;
  }
  steps_[current_].status = PlanStep::Done;
  current_++;
  if (current_ < steps_.size()) {
    steps_[current_].status = PlanStep::Active;
  }
}

void Planner::skip(size_t idx) {
  if (idx >= steps_.size()) {
    return;
  }
  steps_[idx].status = PlanStep::Skipped;
}

std::string Planner::to_markdown() const {
  std::ostringstream oss;
  oss << "# Plan: " << goal_ << "\n\n";
  for (size_t i = 0; i < steps_.size(); i++) {
    const auto &s = steps_[i];
    std::string marker = "[ ]";
    if (s.status == PlanStep::Done) {
      marker = "[x]";
    } else if (s.status == PlanStep::Active) {
      marker = "[>]";
    } else if (s.status == PlanStep::Skipped) {
      marker = "[-]";
    }
    oss << marker << " " << s.description << "\n";
  }
  return oss.str();
}

bool Planner::complete() const {
  for (const auto &s : steps_) {
    if (s.status == PlanStep::Pending || s.status == PlanStep::Active) {
      return false;
    }
  }
  return true;
}

const std::string &Planner::goal() const {
  return goal_;
}

size_t Planner::current() const {
  return current_;
}