// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <string>
#include "graph.h"

// Test cases for tool_graph
static void test_valid_bar_chart() {
  std::cout << "\n=== Test: Valid Bar Chart ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "House pets",
    "percent": false,
    "suffix": "pts",
    "data": [
      {"label": "Spiders", "value": 20.0},
      {"label": "Cats", "value": 4},
      {"label": "Dogs", "value": 1},
      {"label": "A lot of ants", "value": 80}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";

  if (result.success_) {
    for (const auto& line : result.data_) {
      std::cout << line << "\n";
    }
  }
}

static void test_valid_tree_view() {
  std::cout << "\n=== Test: Valid Tree View ===\n";

  std::string json = R"({
    "type": "tree",
    "title": "Project Structure",
    "data": [
      {"label": "nitro", "value": 100},
      {"label": "src", "value": 85},
      {"label": "include", "value": 72},
      {"label": "tests", "value": 65}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";

  if (result.success_) {
    for (const auto& line : result.data_) {
      std::cout << line << "\n";
    }
  }
}

static void test_invalid_json() {
  std::cout << "\n=== Test: Invalid JSON ===\n";

  std::string json = "not valid json {{{";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(!result.success_);
  assert(result.message_.find("ERROR") != std::string::npos);
}

static void test_empty_data() {
  std::cout << "\n=== Test: Empty Data ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Empty Chart",
    "data": []
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(!result.success_);
  assert(result.message_.find("ERROR") != std::string::npos);
}

static void test_unsupported_type() {
  std::cout << "\n=== Test: Unsupported Graph Type ===\n";

  std::string json = R"({
    "type": "pie",
    "title": "Pie Chart",
    "data": [
      {"label": "A", "value": 50},
      {"label": "B", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(!result.success_);
  assert(result.message_.find("ERROR") != std::string::npos);
}

static void test_minimal_bar_chart() {
  std::cout << "\n=== Test: Minimal Bar Chart ===\n";

  std::string json = R"({
    "type": "bar",
    "data": [
      {"label": "X", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";

  if (result.success_) {
    for (const auto& line : result.data_) {
      std::cout << line << "\n";
    }
  }
}

static void test_large_dataset() {
  std::cout << "\n=== Test: Large Dataset ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Performance Metrics",
    "data": [
      {"label": "CPU", "value": 95},
      {"label": "GPU", "value": 88},
      {"label": "RAM", "value": 76},
      {"label": "SSD", "value": 92},
      {"label": "NET", "value": 65},
      {"label": "DISK", "value": 81},
      {"label": "IO", "value": 73},
      {"label": "MEM", "value": 89}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";

  if (result.success_) {
    for (const auto& line : result.data_) {
      std::cout << line << "\n";
    }
  }
}

static void test_bar_chart() {
  std::cout << "\n=== Test: Bar Chart ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Memory Usage",
    "percent": true,
    "precision": 1,
    "data": [
      {"label": "Heap", "value": 75},
      {"label": "Stack", "value": 25},
      {"label": "Static", "value": 10}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";

  if (result.success_) {
    for (const auto& line : result.data_) {
      std::cout << line << "\n";
    }
  }
  assert(result.success_);
}

static void test_tree_with_nested_children() {
  std::cout << "\n=== Test: Tree with Nested Children ===\n";

  std::string json = R"({
    "title": "Project Structure",
    "type": "tree",
    "data": [{
      "label": "level 1.1",
      "children": [{
        "label": "level 2.1",
        "value": 111,
        "children": [{
          "label": "level 3.1",
          "value": 222.3,
          "children": [{
            "label": "level 4.1",
            "value": 222.44
          }, {
            "label": "level 4.2",
            "value": 333.44
          }]
        }, {
          "label": "level 3.2",
          "value": 333.3
        }]
      }, {
        "label": "level 2.2",
        "value": 444.2,
        "children": [{
          "label": "level 3.1",
          "value": 555.3
          }
        ]}
      ]}, {
        "label": "level 1.2",
        "value": 666.1,
        "children": [{
          "label": "2.1",
          "value": 777.2
        },{
          "label": "2.2",
          "value": 888.2
        }]
    }]
   })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";

  if (result.success_) {
    for (const auto& line : result.data_) {
      std::cout << line << "\n";
    }
  }
  assert(result.success_);
}

static void test_zero_values() {
  std::cout << "\n=== Test: Zero Values ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Zero Test",
    "data": [
      {"label": "A", "value": 0},
      {"label": "B", "value": 50},
      {"label": "C", "value": 0}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_single_value() {
  std::cout << "\n=== Test: Single Value ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Single",
    "data": [
      {"label": "Only", "value": 100}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_very_small_canvas() {
  std::cout << "\n=== Test: Very Small Canvas ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Small",
    "data": [
      {"label": "X", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";
  assert(result.success_);
}

static void test_very_large_values() {
  std::cout << "\n=== Test: Very Large Values ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Large Values",
    "data": [
      {"label": "A", "value": 9999},
      {"label": "B", "value": 8888},
      {"label": "C", "value": 7777}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_decimal_values() {
  std::cout << "\n=== Test: Decimal Values ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Decimals",
    "data": [
      {"label": "A", "value": 33.33},
      {"label": "B", "value": 66.66},
      {"label": "C", "value": 12.34}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_no_title() {
  std::cout << "\n=== Test: No Title ===\n";

  std::string json = R"({
    "type": "bar",
    "data": [
      {"label": "A", "value": 50},
      {"label": "B", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_no_color() {
  std::cout << "\n=== Test: No Color Specified ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "No Color",
    "data": [
      {"label": "A", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_very_long_labels() {
  std::cout << "\n=== Test: Very Long Labels ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Long Labels",
    "data": [
      {"label": "ThisIsAVeryLongLabelThatShouldBeTruncated", "value": 50},
      {"label": "AnotherVeryLongLabelHere", "value": 60}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_special_characters_in_labels() {
  std::cout << "\n=== Test: Special Characters in Labels ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Special Chars",
    "data": [
      {"label": "CPU@100%", "value": 95},
      {"label": "GPU#88", "value": 88},
      {"label": "RAM$76", "value": 76}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_width_constraint() {
  std::cout << "\n=== Test: Width Constraint Applied ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Width Test",
    "data": [
      {"label": "A", "value": 50},
      {"label": "B", "value": 60},
      {"label": "C", "value": 70},
      {"label": "D", "value": 80},
      {"label": "E", "value": 90}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";
  assert(result.success_);
}

static void test_height_constraint() {
  std::cout << "\n=== Test: Height Constraint Applied ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Height Test",
    "data": [
      {"label": "A", "value": 50},
      {"label": "B", "value": 60},
      {"label": "C", "value": 70}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_all_same_values() {
  std::cout << "\n=== Test: All Same Values ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Equal Values",
    "data": [
      {"label": "A", "value": 50},
      {"label": "B", "value": 50},
      {"label": "C", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_monotonic_increasing() {
  std::cout << "\n=== Test: Monotonic Increasing ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Increasing",
    "data": [
      {"label": "A", "value": 10},
      {"label": "B", "value": 20},
      {"label": "C", "value": 30},
      {"label": "D", "value": 40}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_monotonic_decreasing() {
  std::cout << "\n=== Test: Monotonic Decreasing ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Decreasing",
    "data": [
      {"label": "A", "value": 100},
      {"label": "B", "value": 80},
      {"label": "C", "value": 60},
      {"label": "D", "value": 40}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_empty_title() {
  std::cout << "\n=== Test: Empty Title ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "",
    "data": [
      {"label": "A", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  assert(result.success_);
}

static void test_very_narrow_canvas() {
  std::cout << "\n=== Test: Very Narrow Canvas ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Narrow",
    "data": [
      {"label": "A", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";
  assert(result.success_);
}

static void test_very_tall_canvas() {
  std::cout << "\n=== Test: Very Tall Canvas ===\n";

  std::string json = R"({
    "type": "bar",
    "title": "Tall",
    "data": [
      {"label": "A", "value": 50}
    ]
  })";

  GraphResult result = tool_graph(80, json);

  std::cout << "Success: " << (result.success_ ? "YES" : "NO") << "\n";
  std::cout << "Message: " << result.message_ << "\n";
  std::cout << "Data size: " << result.data_.size() << "\n";
  assert(result.success_);
}

void graph_test() {
  std::cout << "========================================\n";
  std::cout << "  Graph Integration Test Suite\n";
  std::cout << "========================================\n";

  test_invalid_json();
  test_empty_data();
  test_unsupported_type();
  test_zero_values();
  test_single_value();
  test_very_small_canvas();
  test_very_large_values();
  test_decimal_values();
  test_no_title();
  test_no_color();
  test_very_long_labels();
  test_special_characters_in_labels();
  test_width_constraint();
  test_height_constraint();
  test_all_same_values();
  test_monotonic_increasing();
  test_monotonic_decreasing();
  test_empty_title();
  test_very_narrow_canvas();
  test_very_tall_canvas();

  // visual tests
  test_large_dataset();
  test_tree_with_nested_children();
  test_valid_tree_view();
  test_valid_bar_chart();
  test_bar_chart();
  test_minimal_bar_chart();
  
  std::cout << "\n========================================\n";
  std::cout << "  All tests completed!\n";
  std::cout << "========================================\n";

}

