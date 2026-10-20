// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <iostream>

#include "planner.h"

static void test_basic_steps() {
  Planner p;
  p.set_goal("Build a web server");
  p.add_step("Create project structure");
  p.add_step("Implement HTTP handler");
  p.add_step("Add tests");

  assert(p.goal() == "Build a web server");
  assert(p.steps().size() == 3);
  assert(p.steps()[0].status == PlanStep::Active);
  assert(p.steps()[1].status == PlanStep::Pending);
  assert(p.steps()[2].status == PlanStep::Pending);
  assert(p.current() == 0);
  assert(!p.complete());
}

static void test_advance() {
  Planner p;
  p.set_goal("Test advance");
  p.add_step("Step 1");
  p.add_step("Step 2");
  p.add_step("Step 3");

  p.advance();
  assert(p.steps()[0].status == PlanStep::Done);
  assert(p.steps()[1].status == PlanStep::Active);
  assert(p.current() == 1);
  assert(!p.complete());

  p.advance();
  assert(p.steps()[1].status == PlanStep::Done);
  assert(p.steps()[2].status == PlanStep::Active);
  assert(p.current() == 2);
  assert(!p.complete());

  p.advance();
  assert(p.steps()[2].status == PlanStep::Done);
  assert(p.current() == 3);
  assert(p.complete());
}

static void test_skip() {
  Planner p;
  p.set_goal("Test skip");
  p.add_step("Step 1");
  p.add_step("Step 2");
  p.add_step("Step 3");

  p.skip(1);
  assert(p.steps()[1].status == PlanStep::Skipped);
  assert(p.steps()[0].status == PlanStep::Active);
  assert(p.steps()[2].status == PlanStep::Pending);
}

static void test_markdown() {
  Planner p;
  p.set_goal("Test markdown");
  p.add_step("Alpha");
  p.add_step("Beta");
  p.add_step("Gamma");

  p.advance();
  p.skip(2);

  std::string md = p.to_markdown();
  assert(md.find("# Plan: Test markdown") != std::string::npos);
  assert(md.find("[x] Alpha") != std::string::npos);
  assert(md.find("[>] Beta") != std::string::npos);
  assert(md.find("[-] Gamma") != std::string::npos);
}

static void test_empty_planner() {
  Planner p;
  assert(p.complete());
  assert(p.current() == 0);
  assert(p.goal().empty());
}

void planner_test() {
  std::cout << "planner_test:\n";
  test_basic_steps();
  test_advance();
  test_skip();
  test_markdown();
  test_empty_planner();
  std::cout << "  all passed\n";
}