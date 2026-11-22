// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// basalt_rag.cpp - No-op RAG stubs for the basalt engine.
//
// Basalt does not support RAG. These stubs satisfy the linker
// so that engine-agnostic code (KnowledgeManager, Agent, etc.)
// can reference RagDB / RagLibrary without pulling in the
// llama-specific implementation.

#include "rag.h"

// ── RagDB ─────────────────────────────────────────────────────

bool RagDB::load(const std::string &path) {
  (void)path;
  return false;
}

bool RagDB::load_mmap(const std::string &path) {
  (void)path;
  return false;
}

bool RagDB::save(const std::string &path) const {
  (void)path;
  return false;
}

bool RagDB::init_file(const std::string &path) const {
  (void)path;
  return false;
}

bool RagDB::append_chunks(const std::string &path) {
  (void)path;
  return false;
}

bool RagDB::finalize_file(const std::string &path, uint32_t n_chunks) {
  (void)path;
  (void)n_chunks;
  return false;
}

int RagDB::repair(const std::string &path) {
  (void)path;
  return -1;
}

std::vector<RagValidationIssue> RagDB::validate() const {
  return {};
}

// ── RagLibrary ────────────────────────────────────────────────

bool RagLibrary::add(const std::string &name, RagDB &&db) {
  (void)name;
  (void)db;
  return false;
}

bool RagLibrary::remove(const std::string &name) {
  (void)name;
  return false;
}

RagDB *RagLibrary::get(const std::string &name) {
  (void)name;
  return nullptr;
}

const RagDB *RagLibrary::get(const std::string &name) const {
  (void)name;
  return nullptr;
}

std::vector<std::string> RagLibrary::names() const {
  return {};
}

int RagLibrary::total_chunks() const {
  return 0;
}

int RagLibrary::load_dir(const std::string &dir) {
  (void)dir;
  return 0;
}

bool RagLibrary::save_dir(const std::string &dir) const {
  (void)dir;
  return false;
}