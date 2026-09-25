// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "mcp_format.h"

static std::string quote(const std::string& value) {
  return "\"" + value + "\"";
}

static std::string shorten(const std::string &desc) {
  // Find first '.' or '\n'
  size_t stopPos = std::string::npos;
  int startPos = 0;
  size_t i = 0;

  while (i < desc.size() && desc[i] == ' ') {
    startPos++;
    i++;
  }

  while (i < desc.size()) {
    if (desc[i] == '.' || desc[i] == '\n') {
      stopPos = i;
      break;
    }
    i++;
  }

  if (stopPos != std::string::npos) {
    return desc.substr(startPos, stopPos);
  }
  return desc;
}

static std::string createExample(const json::JsonValue &propObj) {
  std::string result = "{";
  std::vector<std::string> keys;
  propObj.get_keys(keys);

  std::string delim;
  for (const auto & key : keys) {
    auto field = propObj.get_child(key);
    std::string type;
    std::string description;

    if (field.get_str("type", type) &&
        field.get_str("description", description)) {
      std::string element;
      if (type == "string") {
        element = quote(shorten(description));
      } else if (type == "array") {
        element = "[item1, item2]";
      } else if (type == "boolean") {
        element = "true";
      } else if (type == "integer") {
        element = "1";
      }
      if (!element.empty()) {
        result += delim;
        result += quote(key) + ":" + element;
        delim = ",";
      }
    }
  }
  result += '}';
  return result;
}

static std::string required(const json::JsonValue &inputSchema) {
  std::string result;
  const auto value = inputSchema.get_child("required");
  std::vector<json::JsonValue> fields;
  value.get_array(fields);
  if (!fields.empty()) {
    result = "Field " + fields[0].get_str() + " is required\n\n";
  } else {
    result = "\n\n";
  }
  return result;
}

std::string formatSpec(const json::JsonValue &root) {
  std::string result;

  json::JsonValue inputSchema = root.get_child("inputSchema");
  json::JsonValue outputSchema = root.get_child("outputSchema");
  std::string name;
  std::string description;

  if (root.get_str("name", name) &&
      root.get_str("description", description) &&
      inputSchema.is_valid()) {
    result = "# Name: " + name + "\n\n";
    result += "## Description\n";
    result += description + "\n";
    result += required(inputSchema);
    result += "## Example request\n";
    result += "```\n";
    result += createExample(inputSchema.get_child("properties"));
    result += "\n```\n";
    if (outputSchema.is_valid()) {
      result += "\n## Response schema\n";
      result += "```\n";
      result += outputSchema.to_string();
      result += "\n```\n";
    }
  }
  return result;
}
