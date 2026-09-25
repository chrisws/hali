// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <string>
#include <fstream>
#include <format>
#include <yyjson.h>
#include "json.h"
#include "logging.h"

namespace json {

//
// JsonValue implementation
//
bool JsonValue::get_str(const std::string &key, std::string &out) const {
  if (!is_valid()) return false;

  const yyjson_val *val = yyjson_obj_get(value_, key.c_str());
  if (!val || !yyjson_is_str(val)) return false;

  out = yyjson_get_str(val);
  return true;
}

bool JsonValue::get_int(const std::string &key, int &out) const {
  if (!is_valid()) return false;

  const yyjson_val *val = yyjson_obj_get(value_, key.c_str());
  if (!val || !yyjson_is_num(val)) return false;

  out = yyjson_get_int(val);
  return true;
}

bool JsonValue::get_bool(const std::string &key, bool &out) const {
  if (!is_valid()) return false;

  const yyjson_val *val = yyjson_obj_get(value_, key.c_str());
  if (!val || !yyjson_is_bool(val)) return false;

  out = yyjson_get_bool(val);
  return true;
}

bool JsonValue::get_float(const std::string &key, float &out) const {
  if (!is_valid()) return false;

  const yyjson_val *val = yyjson_obj_get(value_, key.c_str());
  if (!val || !yyjson_is_num(val)) return false;

  out = yyjson_get_num(val);
  return true;
}

JsonValue JsonValue::get_child(const std::string &key) const {
  return JsonValue(yyjson_obj_get(value_, key.c_str()));
}

bool JsonValue::get_array(const std::string &key, std::vector<JsonValue> &out) const {
  if (!is_valid()) return false;

  yyjson_val *val = yyjson_obj_get(value_, key.c_str());
  if (!val || !yyjson_is_arr(val)) return false;

  out.reserve(yyjson_arr_size(val));
  for (size_t i = 0; i < yyjson_arr_size(val); i++) {
    if (yyjson_val *item = yyjson_arr_get(val, i)) {
      out.emplace_back(item);
    }
  }
  return true;
}

bool JsonValue::get_keys(std::vector<std::string> &out) const {
  if (!is_valid()) return false;

  yyjson_obj_iter iter;
  yyjson_obj_iter_init(value_, &iter);
  for (yyjson_val* key_ptr = yyjson_obj_iter_next(&iter); key_ptr != nullptr; key_ptr = yyjson_obj_iter_next(&iter)) {
    out.emplace_back(yyjson_get_str(key_ptr));
  }
  return true;
}

bool JsonValue::has_string_key(const std::string &key) const {
  if (!is_valid()) return false;

  yyjson_val *val = yyjson_obj_get(value_, key.c_str());
  return (val != nullptr && yyjson_is_str(val));
}

std::string JsonValue::to_string(WriteFlag flags) const {
  if (!is_valid()) return "";
  char *buffer = yyjson_val_write(value_, static_cast<yyjson_write_flag>(flags), nullptr);
  if (!buffer) return "";
  std::string result(buffer);
  free(buffer);
  return result;
}

std::string JsonValue::get_str() const {
  std::string result;
  if (is_valid() && is_str()) {
    result = yyjson_get_str(value_);
  }
  return result;
}

void JsonValue::get_array(std::vector<JsonValue> &out) const {
  if (is_valid() && is_arr()) {
    out.reserve(yyjson_arr_size(value_));
    for (size_t i = 0; i < yyjson_arr_size(value_); i++) {
      if (yyjson_val *item = yyjson_arr_get(value_, i)) {
        out.emplace_back(item);
      }
    }
  }
}

//
// JsonDoc implementation
//
JsonDoc parse(const std::string &json_str) {
  return JsonDoc::parse(json_str);
}

JsonDoc JsonDoc::parse(const std::string &json_str) {
  yyjson_read_err err;
  yyjson_doc *doc = yyjson_read_opts(const_cast<char*>(json_str.data()), json_str.size(), 0, nullptr, &err);
  if (!doc && err.code == YYJSON_READ_ERROR_UNEXPECTED_END) {
    // in case the model omitted final "}"
    const std::string &json_fix = json_str + "}";
    doc = yyjson_read_opts(const_cast<char*>(json_fix.data()), json_fix.size(), 0, nullptr, &err);
  }
  if (!doc) {
    log_write(LEVEL_INFO, "json error: [%d] [%s]", err.code, err.msg);
  }
  return JsonDoc(doc);
}

JsonValue JsonDoc::get_root() const {
  if (doc_ != nullptr) {
    if (yyjson_val *root = yyjson_doc_get_root(doc_); root && yyjson_is_obj(root)) {
      return JsonValue(root);
    }
  }
  return JsonValue{};
}

std::string JsonDoc::to_string(WriteFlag flags) const {
  if (!is_valid()) return "";
  char *buffer = yyjson_write(doc_, static_cast<yyjson_write_flag>(flags), nullptr);
  if (!buffer) return "";
  std::string result(buffer);
  free(buffer);
  return result;
}

bool JsonDoc::write_file(const std::string &path, WriteFlag flags) const {
  std::ofstream file(path);
  if (!file.is_open()) return false;
  file << to_string(flags);
  return true;
}

//
// JsonMutValue implementation
//
bool JsonMutValue::set_str(const std::string &key, const std::string &value) {
  if (!is_valid()) return false;
  return yyjson_mut_obj_add(value_, yyjson_mut_strcpy(doc_, key.c_str()), yyjson_mut_strcpy(doc_, value.c_str()));
}

bool JsonMutValue::set_empty_obj(const std::string &key) {
  if (!is_valid()) return false;
  return yyjson_mut_obj_add(value_, yyjson_mut_strcpy(doc_, key.c_str()), yyjson_mut_obj(doc_));
}

bool JsonMutValue::set_int(const std::string &key, int value) {
  if (!is_valid()) return false;
  return yyjson_mut_obj_add(value_, yyjson_mut_strcpy(doc_, key.c_str()), yyjson_mut_int(doc_, value));
}

bool JsonMutValue::set_bool(const std::string &key, bool value) {
  if (!is_valid()) return false;
  return yyjson_mut_obj_add(value_, yyjson_mut_strcpy(doc_, key.c_str()), yyjson_mut_bool(doc_, value));
}

JsonMutValue JsonMutValue::get_child(const std::string &key) const {
  if (!is_valid()) return JsonMutValue(nullptr, nullptr);
  // creates a new empty object value allocated in doc_'s memory pool
  // it's not attached to anything until you add it somewhere.
  yyjson_mut_val *child = yyjson_mut_obj(doc_);

  // attach it by passing it directly as the value argument to yyjson_mut_obj_add on the parent
  yyjson_mut_obj_add(value_, yyjson_mut_strcpy(doc_, key.c_str()), child);
  return JsonMutValue(doc_, child);
}

bool JsonMutValue::set_obj(const std::string &key, const std::string &value) {
  if (!is_valid()) return false;
  auto valueDoc = JsonMutDoc::parse(value);
  if (!valueDoc.is_valid()) return false;
  auto valueRoot = valueDoc.get_root();
  if (!valueRoot.is_valid()) return false;

  return yyjson_mut_obj_add(value_, yyjson_mut_strcpy(doc_, key.c_str()), valueRoot.value_);
}

//
// JsonMutDoc implementation
//
JsonMutDoc parse_mutable(const std::string &json_str) {
  return JsonMutDoc::parse(json_str);
}

JsonMutDoc JsonMutDoc::parse(const std::string &json_str) {
  yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
  if (!doc) return JsonMutDoc(nullptr);

  if (json_str.empty()) {
    // no input - create an empty object as root so callers can add to it
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
  } else {
    yyjson_read_err err;
    if (yyjson_doc *parsed = yyjson_read_opts(const_cast<char*>(json_str.data()), json_str.size(), 0, nullptr, &err)) {
      if (!parsed) {
        log_write(LEVEL_INFO, "json error: [%d] [%s]", err.code, err.msg);
      } else {
        const yyjson_val *parsed_root = yyjson_doc_get_root(parsed);
        yyjson_mut_val *root_copy = yyjson_val_mut_copy(doc, parsed_root);
        yyjson_mut_doc_set_root(doc, root_copy);
        yyjson_doc_free(parsed);
      }
    } else {
      // parse failed - fall back to an empty object so the doc is still usable
      yyjson_mut_val *root = yyjson_mut_obj(doc);
      yyjson_mut_doc_set_root(doc, root);
    }
  }

  return JsonMutDoc(doc);
}

JsonMutValue JsonMutDoc::get_root() const {
  if (doc_ != nullptr) {
    auto *root = yyjson_mut_doc_get_root(doc_);
    if (root && yyjson_mut_is_obj(root)) {
      return JsonMutValue(doc_, root);
    }
  }
  return JsonMutValue{};
}

JsonMutValue JsonMutDoc::get_child(const std::string &key) const {
  if (!is_valid()) return JsonMutValue(nullptr, nullptr);

  yyjson_mut_val *child = yyjson_mut_obj(doc_);

  // attach the child to the root object
  auto *root = yyjson_mut_doc_get_root(doc_);
  yyjson_mut_obj_add(root, yyjson_mut_strcpy(doc_, key.c_str()), child);

  return JsonMutValue(doc_, child);
}

std::string JsonMutDoc::to_string(WriteFlag flags) const {
  if (!is_valid()) return "";
  char *buffer = yyjson_mut_write(doc_, static_cast<yyjson_write_flag>(flags), nullptr);
  if (!buffer) return "";
  std::string result(buffer);
  free(buffer);
  return result;
}

} // namespace json

