// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <vector>
#include "yyjson.h"

//
// JSON dependency inversion layer header
// Provides a C++ wrapper around yyjson for JSON parsing and manipulation
//

namespace json {

// Parse flags for yyjson_write
enum class WriteFlag {
  Pretty = 1,
  Compact = 0
};

struct JsonValue {
  JsonValue() = default;
  ~JsonValue() = default;
  explicit JsonValue(yyjson_val *value) : value_(value) {}

  bool get_array(const std::string &key, std::vector<JsonValue> &out) const;
  bool get_bool(const std::string &key, bool &out) const;
  bool get_float(const std::string &key, float &out) const;
  bool get_int(const std::string &key, int &out) const;
  bool get_keys(std::vector<std::string> &out) const;
  bool get_str(const std::string &key, std::string &out) const;
  bool has_string_key(const std::string &key) const;
  bool is_arr() const { return value_ && yyjson_is_arr(value_); }
  bool is_str() const { return value_ && yyjson_is_str(value_); }
  bool is_object() const { return value_ && yyjson_is_obj(value_); }
  bool is_valid() const { return value_ != nullptr; }
  JsonValue get_child(const std::string &key) const;
  void get_array(std::vector<JsonValue> &out) const;
  std::string to_string(WriteFlag flags = WriteFlag::Compact) const;
  std::string get_str() const;

  private:
  yyjson_val *value_;
};

struct JsonMutValue {
  JsonMutValue() = default;
  ~JsonMutValue() = default;

  explicit JsonMutValue(yyjson_mut_doc *doc, yyjson_mut_val *value)
    : doc_(doc)
    , value_(value) {
  }

  bool is_valid() const { return value_ != nullptr; }
  bool is_arr() const { return value_ && yyjson_mut_is_arr(value_); }
  bool set_str(const std::string &key, const std::string &value);
  bool set_int(const std::string &key, int value);
  bool set_bool(const std::string &key, bool value);
  bool set_empty_obj(const std::string &key);
  bool set_obj(const std::string &key, const std::string &value);
  JsonMutValue get_child(const std::string &key) const;

  private:
  yyjson_mut_doc *doc_;
  yyjson_mut_val *value_;
};

class JsonDoc {
public:
  JsonDoc() = default;
  explicit JsonDoc(yyjson_doc *doc) : doc_(doc) {}
  static JsonDoc parse(const std::string &json_str);

  ~JsonDoc() {
    yyjson_doc_free(doc_);
    doc_ = nullptr;
  }

  // Non-copyable but movable
  JsonDoc(const JsonDoc&) = delete;
  JsonDoc &operator=(const JsonDoc&) = delete;
  JsonDoc(JsonDoc &&other) noexcept : doc_(other.doc_) { other.doc_ = nullptr; }
  JsonDoc &operator=(JsonDoc &&other) noexcept {
    if (this != &other) {
      if (doc_) yyjson_doc_free(doc_);
      doc_ = other.doc_;
      other.doc_ = nullptr;
    }
    return *this;
  }

  JsonValue get_root() const;
  std::string to_string(WriteFlag flags) const;
  bool write_file(const std::string &path, WriteFlag flags) const;
  bool is_valid() const { return doc_ != nullptr; }

private:
  yyjson_doc *doc_ = nullptr;
};

// Mutable veneer class around yyjson_mut_doc for building/modifying JSON
class JsonMutDoc {
public:
  JsonMutDoc() = default;
  explicit JsonMutDoc(yyjson_mut_doc *doc) : doc_(doc) {}
  static JsonMutDoc parse(const std::string &json_str);

  ~JsonMutDoc() {
    yyjson_mut_doc_free(doc_);
    doc_ = nullptr;
  }

  // Non-copyable but movable
  JsonMutDoc(const JsonMutDoc&) = delete;
  JsonMutDoc &operator=(const JsonMutDoc &) = delete;
  JsonMutDoc(JsonMutDoc &&other) noexcept : doc_(other.doc_) { other.doc_ = nullptr; }
  JsonMutDoc &operator=(JsonMutDoc &&other) noexcept {
    if (this != &other) {
      if (doc_) yyjson_mut_doc_free(doc_);
      doc_ = other.doc_;
      other.doc_ = nullptr;
    }
    return *this;
  }

  JsonMutValue get_root() const;
  JsonMutValue get_child(const std::string &key) const;
  std::string to_string(WriteFlag flags = WriteFlag::Compact) const;
  bool is_valid() const { return doc_ != nullptr; }

private:
  yyjson_mut_doc *doc_ = nullptr;
};

/**
 * Create a JsonDoc from a string
 * @param json_str The JSON string
 * @return JsonDoc wrapper around the parsed document
 */
JsonDoc parse(const std::string &json_str);

/**
 * Create a JsonMutDoc from a string
 * @param json_str The JSON string
 * @return JsonMutDoc wrapper around the parsed mutable document
 */
JsonMutDoc parse_mutable(const std::string &json_str);

} // namespace json

