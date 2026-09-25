// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <regex>
#include "graph.h"
#include <format>
#include "json.h"

constexpr std::string g_horz_bar = "─";
constexpr std::string g_vert_bar = "│";
constexpr std::string g_block = "█";
constexpr std::string g_block_light = "░";
constexpr std::string g_box_top_left = "┌";
constexpr std::string g_box_top_right = "┐";
constexpr std::string g_box_bottom_left = "└";
constexpr std::string g_box_bottom_right = "┘";
constexpr std::string g_box_left_pipe = "├";
constexpr std::string g_box_right_pipe = "┤";
constexpr std::string g_tree_bottom = "└──";
constexpr std::string g_tree_mid = "├──";
constexpr std::string g_tree_top = "┌──";

//
// Graph data structure for visualization
//
struct TreeNode {
  TreeNode() = default;
  virtual ~TreeNode() = default;
  std::string label;
  float value = 0.0f;
  std::vector<TreeNode> children;
};

struct GraphData {
  GraphData() : rows_(0), precision_(0), percent_(false) {}
  virtual ~GraphData() = default;

  std::string title_;
  std::string type_;
  std::string suffix_;
  std::vector<std::pair<std::string, float>> data_;
  std::vector<TreeNode> tree_nodes_;
  int rows_;
  int precision_;
  bool percent_;

  bool isValid() const;
};

//
// Canvas class for drawing operations
//
class Canvas {
  public:
  Canvas(size_t width, size_t height)
    : width_(width)
    , height_(height) {
    buffer_.resize(height, std::vector<std::string>(width, " "));
  }

  void draw_vline(int x, int y1, int y2);
  void draw_hline(int x, int y1, int x2);
  void draw_text(int x, int y, const std::string &str);
  void draw_char(int x, int y, const std::string &str);

  // Set a unicode character (as string) at position (y, x)
  void insert_at(size_t y, size_t x, const std::string &str);

  // Render the canvas by concatenating all columns into rows
  std::vector<std::string> render() const;

  int get_width() const { return width_; }
  int get_height() const { return height_; }

  private:
  std::vector<std::vector<std::string>> buffer_;
  int width_ = 0;
  int height_ = 0;
};

std::vector<std::string> Canvas::render() const {
  std::vector<std::string> result;
  result.reserve(height_);
  for (size_t y = 0; y < height_; ++y) {
    std::string row;
    // Reserve space for potential multi-byte chars
    row.reserve(width_ * 4);
    for (size_t x = 0; x < width_; ++x) {
      row += buffer_[y][x];
    }
    result.push_back(row);
  }
  return result;
}

void Canvas::insert_at(size_t y, size_t x, const std::string &str) {
  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    return;
  }
  buffer_[y][x] = str;
}

void Canvas::draw_vline(int x, int y1, int y2) {
  if (x < 0 || x >= width_) return;
  int startY = std::max(0, y1);
  int endY = std::min(height_ - 1, y2);
  for (int y = startY; y <= endY; ++y) {
    insert_at(y, x, g_vert_bar);
  }
}

void Canvas::draw_hline(int x, int y1, int x2) {
  if (y1 < 0 || y1 >= height_) {
    return;
  }
  int startX = std::max(0, x);
  int endX = std::min(width_ - 1, x2);
  for (int i = startX; i <= endX; ++i) {
    insert_at(y1, i, g_horz_bar);
  }
}

void Canvas::draw_text(int x, int y, const std::string &str) {
  if (y < 0 || y >= height_) {
    return;
  }
  for (size_t i = 0; i < str.length(); ++i) {
    int pos = x + static_cast<int>(i);
    if (pos >= 0 && pos < width_) {
      buffer_[y][pos] = str[i];
    }
  }
}

void Canvas::draw_char(int x, int y, const std::string &str) {
  insert_at(y, x, str);
}

//
// GraphData validation
//
bool GraphData::isValid() const {
  if (type_.empty()) return false;
  if (data_.empty()) return false;
  return true;
}

//
// JSON parsing
//
static TreeNode parse_tree_node(const json::JsonValue &item, int &rows) {
  TreeNode node;
  if (item.is_object()) {
    item.get_str("label", node.label);
    item.get_float("value", node.value);
    rows++;

    // Parse children if present
    std::vector<json::JsonValue> children_arr;
    if (item.get_array("children", children_arr)) {
      for (const auto &child : children_arr) {
        node.children.push_back(parse_tree_node(child, rows));
      }
    }
  }
  return node;
}

static std::optional<GraphData> parse(const std::string &json) {
  const json::JsonDoc doc = json::parse(json);
  if (!doc.is_valid()) {
    return std::nullopt;
  }

  const json::JsonValue root = doc.get_root();
  if (!root.is_object()) {
    return std::nullopt;
  }

  GraphData result;

  // Extract string fields
  root.get_str("title", result.title_);
  root.get_str("type", result.type_);
  root.get_str("suffix", result.suffix_);
  root.get_int("precision", result.precision_);
  root.get_bool("percent", result.percent_);

  // Parse data array
  std::vector<json::JsonValue> data_arr;
  if (root.get_array("data", data_arr)) {
    for (const auto &item : data_arr) {
      if (!item.is_object()) continue;

      std::string label;
      float value = 0.0f;
      item.get_str("label", label);
      item.get_float("value", value);
      result.rows_++;
      result.data_.emplace_back(label, value);
    }
  }

  // Parse tree nodes for tree type
  if (result.type_ == "tree") {
    std::vector<json::JsonValue> tree_arr;
    if (root.get_array("data", tree_arr)) {
      for (const auto &item : tree_arr) {
        result.tree_nodes_.push_back(parse_tree_node(item, result.rows_));
      }
    }
  }

  if (result.isValid() && !result.data_.empty()) {
    return result;
  }
  return std::nullopt;
}

//
// Rendering functions
//
static void render_title(Canvas &canvas, const GraphData &data) {
  const int width = canvas.get_width();
  canvas.draw_hline(1, 0, width - 2);
  canvas.draw_hline(1, 2, width - 2);
  canvas.draw_vline(0, 1, 1);
  canvas.draw_vline(width - 1, 1, 1);
  canvas.draw_char(0, 0, g_box_top_left);
  canvas.draw_char(width - 1, 0, g_box_top_right);
  canvas.draw_char(0, 2, g_box_bottom_left);
  canvas.draw_char(width - 1, 2, g_box_bottom_right);
  if (!data.title_.empty()) {
    canvas.draw_text(2, 1, data.title_);
  }
}

static void render_bar_chart(Canvas &canvas, GraphData &data) {
  const int width = canvas.get_width();
  const int height = canvas.get_height();

  // Find max value for scaling and longest label
  float scale_value = 0;
  size_t longest_label = 0;
  for (const auto &[first, second] : data.data_) {
    if (data.percent_) {
      // display data as a percentage
      scale_value += second;
    } else if (second > scale_value) {
      // display relative to the largest value
      scale_value = second;
    }
    if (const size_t label_len = first.length(); label_len > longest_label) {
      longest_label = label_len;
    }
  }
  if (scale_value == 0) {
    scale_value = 1;
  }

  // Calculate bar dimensions
  const int bar_area_left = longest_label + 4;
  const int bar_area_width = width - bar_area_left - 14;

  // Draw axes and joiners
  canvas.draw_hline(1, height - 1, width - 2);
  canvas.draw_vline(0, 3, height - 1);
  canvas.draw_char(0, 2, g_box_left_pipe);
  canvas.draw_char(0, height - 1, g_box_bottom_left);
  canvas.draw_vline(width - 1, 3, height - 2);
  canvas.draw_char(width - 1, 2, g_box_right_pipe);
  canvas.draw_char(width - 1, height - 1, g_box_bottom_right);

  if (!data.percent_) {
    // sort the data in order of largest value to smallest
    std::ranges::sort(data.data_, [](const auto &a, const auto &b) {
      return a.second > b.second;
    });
  }

  // Draw bars (horizontal)
  for (size_t i = 0; i < data.data_.size(); ++i) {
    const int y = 3 + i;
    const int bar_width = std::max(1, static_cast<int>(data.data_[i].second * bar_area_width / scale_value));

    // Draw label with space after it
    if (!data.data_[i].first.empty()) {
      canvas.draw_text(2, y, data.data_[i].first + " ");
    }

    // Draw bar
    for (int x = 0; x < bar_area_width; ++x) {
      // for all of the bar_area
      if (x < bar_width) {
        canvas.draw_char(bar_area_left + x, y, g_block);
      } else {
        canvas.draw_char(bar_area_left + x, y, g_block_light);
      }
    }

    // Draw value
    const auto value = data.data_[i].second;
    std::ostringstream value_str;
    if (data.percent_) {
      value_str << std::fixed << std::setprecision(data.precision_) << (value * 100 / scale_value) << "%";
    } else {
      value_str << std::fixed << std::setprecision(data.precision_) << (value) << " " << data.suffix_;
    }
    canvas.draw_text(bar_area_left + bar_area_width + 1, y, value_str.str());
  }
}

static void render_tree_node(Canvas &canvas, const GraphData &data, const TreeNode &node, int indent, const std::string &prefix, int &y, bool last_parent) {
  if (const int height = canvas.get_height(); y >= height - 1) {
    return;
  }
  std::ostringstream line;
  if (const std::string value = std::format("({})", node.value); value == "(0)") {
    line << prefix << " " << node.label;
  } else {
    line << prefix << " " << node.label << " " << value;
  }

  canvas.draw_text(1, y, line.str());
  y++;

  for (int i = 0; i < node.children.size(); i++) {
    std::string child_prefix;
    for (int j = 0; j < indent; j++) {
      if ((!last_parent || j == 0) && j % 4 == 0) {
        child_prefix += g_vert_bar;
      } else {
        child_prefix += " ";
      }
    }
    if (i == node.children.size() - 1) {
      child_prefix += g_tree_bottom;
    } else {
      child_prefix += g_tree_mid;
    }
    auto &child_node = node.children[i];
    const bool last_child_parent = (i == node.children.size() - 1);
    render_tree_node(canvas, data, child_node, indent + 4, child_prefix, y, last_child_parent);
  }
}

static void render_tree_view(Canvas &canvas, const GraphData &data) {
  int y = 3;
  for (int i = 0; i < data.tree_nodes_.size(); i++) {
    std::string child_prefix;
    if (i == 0) {
      child_prefix = g_tree_top;
    } else {
      child_prefix = g_tree_mid;
    }
    const bool last_parent = (i == data.tree_nodes_.size()- 1);
    auto &node = data.tree_nodes_[i];
    render_tree_node(canvas, data, node, 4, child_prefix, y, last_parent);
  }
}

GraphResult tool_graph(const int term_cols, const std::string &graph_json) {
  GraphResult result;
  auto data = parse(graph_json);
  if (!data) {
    result.set_error("Failed to parse graph data");
  } else if (data->data_.empty()) {
    result.set_error("No data to render");
  } else if (!data->isValid()) {
    result.set_error("Invalid graph data");
  } else if (data->type_ != "bar" && data->type_ != "tree") {
    result.set_error("Unsupported graph type");
  } else {
    const int rows = data->rows_ + (data->type_ == "bar" ? 4 : 2);
    Canvas canvas(term_cols, rows);
    render_title(canvas, *data);
    if (data->type_ == "bar") {
      render_bar_chart(canvas, *data);
    } else {
      render_tree_view(canvas, *data);
    }
    result.data_ = canvas.render();
  }
  return result;
}

