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
#include <fstream>
#include <filesystem>

#include "config.h"
#include "json.h"

using namespace std;

static void test_set_value_float() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("temperature", "0.5", reparam, msg);
  assert(rc == 0);
  assert(cfg.temperature_ == 0.5f);
  assert(reparam == true);

  rc = cfg.set_value("top_p", "0.9", reparam, msg);
  assert(rc == 0);
  assert(cfg.top_p_ == 0.9f);
  assert(reparam == true);

  rc = cfg.set_value("min_p", "0.1", reparam, msg);
  assert(rc == 0);
  assert(cfg.min_p_ == 0.1f);
  assert(reparam == true);

  cout << "test_set_value_float passed" << endl;
}

static void test_set_value_int() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("top_k", "40", reparam, msg);
  assert(rc == 0);
  assert(cfg.top_k_ == 40);
  assert(reparam == true);

  rc = cfg.set_value("penalty_last_n", "512", reparam, msg);
  assert(rc == 0);
  assert(cfg.penalty_last_n_ == 512);
  assert(reparam == true);

  rc = cfg.set_value("rag_top_k", "10", reparam, msg);
  assert(rc == 0);
  assert(cfg.rag_top_k_ == 10);
  assert(reparam == false);

  cout << "test_set_value_int passed" << endl;
}

static void test_set_value_bool() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("offload_kqv", "true", reparam, msg);
  assert(rc == 0);
  assert(cfg.offload_kqv_ == true);

  rc = cfg.set_value("offload_kqv", "false", reparam, msg);
  assert(rc == 0);
  assert(cfg.offload_kqv_ == false);

  cout << "test_set_value_bool passed" << endl;
}

static void test_set_value_unknown_key() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("nonexistent", "1", reparam, msg);
  assert(rc == 1);

  cout << "test_set_value_unknown_key passed" << endl;
}

static void test_set_value_parse_error() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("temperature", "not_a_number", reparam, msg);
  assert(rc == 2);
  assert(!msg.empty());

  cout << "test_set_value_parse_error passed" << endl;
}

static void test_set_value_run_allowed() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("run_allowed", "ls,cat,grep", reparam, msg);
  assert(rc == 0);
  assert(cfg.run_allowed_.size() == 3);
  assert(cfg.run_allowed_[0] == "ls");
  assert(cfg.run_allowed_[1] == "cat");
  assert(cfg.run_allowed_[2] == "grep");

  rc = cfg.set_value("run_allowed", "none", reparam, msg);
  assert(rc == 0);
  assert(cfg.run_allowed_.empty());

  cout << "test_set_value_run_allowed passed" << endl;
}

static void test_set_value_gpu_layers() {
  HaliConfig cfg;
  bool reparam = false;
  string msg;

  int rc = cfg.set_value("n_gpu_layers", "48", reparam, msg);
  assert(rc == 0);
  assert(cfg.n_gpu_layers_ == 48);
  assert(msg.find("next /model load") != string::npos);

  cout << "test_set_value_gpu_layers passed" << endl;
}

static void test_introspect_nested() {
  HaliConfig cfg;
  cfg.model_path_ = "/tmp/test.gguf";
  cfg.rag_top_k_ = 8;
  cfg.mtp_enabled_ = true;
  cfg.mtp_n_max_ = 5;
  cfg.mcp_host_ = "10.0.0.1";
  cfg.mcp_port_ = 9999;
  cfg.log_level_str_ = "2";

  string json = cfg.introspect();

  // Verify nested structure
  assert(json.find("\"model\"") != string::npos);
  assert(json.find("\"rag\"") != string::npos);
  assert(json.find("\"mtp\"") != string::npos);
  assert(json.find("\"suffix\"") != string::npos);
  assert(json.find("\"audio\"") != string::npos);
  assert(json.find("\"mcp\"") != string::npos);
  assert(json.find("\"logging\"") != string::npos);

  // Verify values are present
  assert(json.find("/tmp/test.gguf") != string::npos);
  assert(json.find("10.0.0.1") != string::npos);
  assert(json.find("9999") != string::npos);

  // Verify it's valid JSON
  auto doc = json::parse(json);
  assert(doc.is_valid());
  auto root = doc.get_root();
  assert(root.is_object());

  auto model = root.get_child("model");
  assert(model.is_object());
  string path;
  assert(model.get_str("path", path));
  assert(path == "/tmp/test.gguf");

  auto rag = root.get_child("rag");
  assert(rag.is_object());
  int rag_k = 0;
  assert(rag.get_int("top_k", rag_k));
  assert(rag_k == 8);
  string embed;
  assert(rag.get_str("embed_path", embed));
  assert(embed == "");

  auto mcp = root.get_child("mcp");
  assert(mcp.is_object());
  string host;
  int port = 0;
  assert(mcp.get_str("host", host));
  assert(mcp.get_int("port", port));
  assert(host == "10.0.0.1");
  assert(port == 9999);

  cout << "test_introspect_nested passed" << endl;
}

static void test_load_settings_nested() {
  // Write a temp config file
  const string path = "/tmp/hali_test_config.json";
  const string content =
    "{\n"
    "  \"model\": { \"path\": \"/tmp/a.gguf\", \"n_ctx\": 1234, \"temperature\": 0.7 },\n"
    "  \"rag\": { \"embed_path\": \"/tmp/bert.gguf\", \"top_k\": 12 },\n"
    "  \"mtp\": { \"enabled\": false, \"n_max\": 7 },\n"
    "  \"mcp\": { \"host\": \"192.168.1.1\", \"port\": 8080 },\n"
    "  \"logging\": { \"level\": \"3\", \"file\": \"/tmp/test.log\" }\n"
    "}\n";
  {
    ofstream f(path);
    f << content;
  }

  HaliConfig cfg;
  cfg.set_config(path);
  cfg.load_settings();

  assert(cfg.model_path_ == "/tmp/a.gguf");
  assert(cfg.n_ctx_ == 1234);
  assert(cfg.temperature_ == 0.7f);
  assert(cfg.rag_top_k_ == 12);
  assert(cfg.embed_path_ == "/tmp/bert.gguf");
  assert(cfg.mtp_enabled_ == false);
  assert(cfg.mtp_n_max_ == 7);
  assert(cfg.mcp_host_ == "192.168.1.1");
  assert(cfg.mcp_port_ == 8080);
  assert(cfg.log_level_str_ == "3");
  assert(cfg.log_file_ == "/tmp/test.log");

  // Cleanup
  std::error_code ec;
  std::filesystem::remove(path, ec);

  cout << "test_load_settings_nested passed" << endl;
}

static void test_introspect_float_precision() {
  HaliConfig cfg;
  cfg.temperature_ = 0.2f;
  cfg.top_p_ = 0.95f;
  cfg.min_p_ = 0.0f;
  cfg.penalty_repeat_ = 1.0f;

  string json = cfg.introspect();

  // Must not contain full-precision float artifacts
  assert(json.find("0.20000000298023224") == string::npos);
  assert(json.find("0.94999998807907104") == string::npos);

  // Verify round-trip: parse back and check values
  auto doc = json::parse(json);
  assert(doc.is_valid());
  auto root = doc.get_root();
  auto model = root.get_child("model");
  float temp = 0;
  assert(model.get_float("temperature", temp));
  assert(temp == 0.2f);
  float top_p = 0;
  assert(model.get_float("top_p", top_p));
  assert(top_p == 0.95f);
  float min_p = 1.0f;
  assert(model.get_float("min_p", min_p));
  assert(min_p == 0.0f);

  cout << "test_introspect_float_precision passed" << endl;
}

static void test_log_erase_default() {
  HaliConfig cfg;
  assert(cfg.log_erase_ == false);
  cout << "test_log_erase_default passed" << endl;
}

static void test_log_erase_load() {
  const string path = "/tmp/hali_test_erase.json";
  const string content =
    "{\n"
    "  \"logging\": { \"level\": \"1\", \"file\": \"/tmp/erase_test.log\", \"erase\": true }\n"
    "}\n";
  {
    ofstream f(path);
    f << content;
  }

  HaliConfig cfg;
  cfg.set_config(path);
  cfg.load_settings();

  assert(cfg.log_erase_ == true);
  assert(cfg.log_file_ == "/tmp/erase_test.log");

  // Verify introspect round-trips the value
  string json = cfg.introspect();
  auto doc = json::parse(json);
  assert(doc.is_valid());
  auto root = doc.get_root();
  auto logging = root.get_child("logging");
  bool erase = false;
  assert(logging.get_bool("erase", erase));
  assert(erase == true);

  // Cleanup
  std::error_code ec;
  std::filesystem::remove(path, ec);

  cout << "test_log_erase_load passed" << endl;
}

static void test_controls_defaults() {
  HaliConfig cfg;
  assert(cfg.disable_write_validate_ == false);
  assert(cfg.disable_patch_validate_ == false);
  assert(cfg.permission_prompt_ == false);
  assert(cfg.disable_run_ == false);
  assert(cfg.disable_save_on_exit_ == false);
  assert(cfg.val_write_size_warn_ == 1000);
  cout << "test_controls_defaults passed" << endl;
}

static void test_controls_load() {
  const string path = "/tmp/hali_test_controls.json";
  const string content =
    "{\n"
    "  \"controls\": {\n"
    "    \"disable_write_validate\": true,\n"
    "    \"disable_patch_validate\": false,\n"
    "    \"permission_prompt\": true,\n"
    "    \"val_write_size_warn\": 8000\n"
    "  }\n"
    "}\n";
  {
    ofstream f(path);
    f << content;
  }

  HaliConfig cfg;
  cfg.set_config(path);
  cfg.load_settings();

  assert(cfg.disable_write_validate_ == true);
  assert(cfg.disable_patch_validate_ == false);
  assert(cfg.permission_prompt_ == true);
  assert(cfg.val_write_size_warn_ == 8000);

  string json = cfg.introspect();
  auto doc = json::parse(json);
  assert(doc.is_valid());
  auto root = doc.get_root();
  auto controls = root.get_child("controls");
  bool v1 = false, v2 = false, pp = false;
  int wsw = 0;
  assert(controls.get_bool("disable_write_validate", v1));
  assert(controls.get_bool("disable_patch_validate", v2));
  assert(controls.get_bool("permission_prompt", pp));
  assert(controls.get_int("val_write_size_warn", wsw));
  assert(v1 == true);
  assert(v2 == false);
  assert(pp == true);
  assert(wsw == 8000);

  std::error_code ec;
  std::filesystem::remove(path, ec);

  cout << "test_controls_load passed" << endl;
}

static void test_system_prompt_extra_dirs() {
  HaliConfig cfg;
  cfg.sandbox_ = "/tmp/sandbox";

  // Without extra dirs - should not mention them
  string prompt = cfg.build_system_prompt();
  assert(prompt.find("extra directories") == string::npos);

  // With extra dirs - should list them
  cfg.extra_dirs_.push_back("/opt/shared");
  cfg.extra_dirs_.push_back("/usr/include");

  prompt = cfg.build_system_prompt();
  assert(prompt.find("extra directories") != string::npos);
  assert(prompt.find("/opt/shared") != string::npos);
  assert(prompt.find("/usr/include") != string::npos);

  cout << "test_system_prompt_extra_dirs passed" << endl;
}

static void test_introspect_knowledge_files_filter() {
  HaliConfig cfg;

  // Simulate what main.cpp does: auto-included + user files
  cfg.knowledge_files_.push_back("AGENTS.md");
  cfg.knowledge_files_.push_back("hali.md");
  cfg.knowledge_files_.push_back("persona.md");
  cfg.knowledge_files_.push_back("my_notes.md");
  cfg.knowledge_files_.push_back("project_guide.md");

  string json = cfg.introspect();

  // Auto-included files must NOT appear in the serialized config
  assert(json.find("AGENTS.md") == string::npos);
  assert(json.find("hali.md") == string::npos);
  assert(json.find("persona.md") == string::npos);

  // User-specified files MUST appear
  assert(json.find("my_notes.md") != string::npos);
  assert(json.find("project_guide.md") != string::npos);

  // Verify the array has exactly 2 entries
  auto doc = json::parse(json);
  assert(doc.is_valid());
  auto root = doc.get_root();
  vector<json::JsonValue> kfs;
  assert(root.get_array("knowledge_files", kfs));
  assert(kfs.size() == 2);

  cout << "test_introspect_knowledge_files_filter passed" << endl;
}

void config_test() {
  cout << "=== Config Tests (plan 12) ===\n\n";
  test_set_value_float();
  test_set_value_int();
  test_set_value_bool();
  test_set_value_unknown_key();
  test_set_value_parse_error();
  test_set_value_run_allowed();
  test_set_value_gpu_layers();
  test_introspect_nested();
  test_load_settings_nested();
  test_introspect_float_precision();
  test_log_erase_default();
  test_log_erase_load();
  test_controls_defaults();
  test_controls_load();
  test_system_prompt_extra_dirs();
  test_introspect_knowledge_files_filter();
  cout << "\nAll config tests passed!\n" << endl;
}