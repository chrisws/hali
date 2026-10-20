// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef PLANNER_H
#define PLANNER_H

#include <string>
#include <vector>

struct PlanStep {
  std::string description;
  enum Status { Pending, Active, Done, Skipped };
  Status status = Pending;
};

class Planner {
public:
  Planner() = default;

  void set_goal(const std::string &goal);
  void add_step(const std::string &description);
  std::vector<PlanStep> steps() const;
  void advance();
  void skip(size_t idx);
  std::string to_markdown() const;
  bool complete() const;
  const std::string &goal() const;
  size_t current() const;

private:
  std::string goal_;
  std::vector<PlanStep> steps_;
  size_t current_ = 0;
};

#endif