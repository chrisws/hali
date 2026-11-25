// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SLASH_UTILS_H
#define SLASH_UTILS_H

#include <functional>
#include <string>
#include <vector>

struct HaliConfig;
struct Agent;
struct Tui;
struct ModelMemoryInfo;
struct FalsePositive;
struct KnowledgeManager;

//
// ── Sampling profiles ────────────────────────────────────────────────────
//
struct SamplingProfile {
  const char *name;
  float temp;
  float top_p;
  int   top_k;
  float min_p;
  float pen_repeat;
  float pen_freq;
  float pen_present;
  int   pen_last_n;
};

void apply_profile(HaliConfig &cfg, const SamplingProfile &p);
void apply_smart_defaults(HaliConfig &cfg, const ModelMemoryInfo &mem, Tui &tui);
void prompt_profile(HaliConfig &cfg, Agent &agent, Tui &tui);

//
// ── Utility helpers ──────────────────────────────────────────────────────
//
std::vector<FalsePositive> parse_false_positives(const std::string &text);

std::string pick_source(Tui &tui, const std::string &arg,
                        const std::vector<std::string> &labels,
                        const std::vector<std::string> &ids,
                        const std::string &prompt,
                        const std::string &empty_msg);

std::vector<std::string> discover_zim_files(const HaliConfig &cfg);

//
// ── Index helpers ────────────────────────────────────────────────────────
//
void index_zim_file(const std::string &path, Agent &agent, Tui &tui,
                    const std::function<void(const std::string &)> &progress,
                    KnowledgeManager &km);
void index_help(Tui &tui);
void index_remove(KnowledgeManager &km, std::string &arg, Tui &tui);
void index_create(const HaliConfig &cfg, Agent &agent, Tui &tui);
void index_load(KnowledgeManager &km, HaliConfig &cfg, Tui &tui);
void index_unload(KnowledgeManager &km, HaliConfig &cfg, std::string &arg, Tui &tui);
void index_list(KnowledgeManager &km, Tui &tui);

//
// ── Slash command implementations ────────────────────────────────────────
//
void slash_audio(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui);
void slash_audit(const std::string &rest, HaliConfig &cfg, Tui &tui, Agent &agent);
void slash_security_audit(const std::string &rest, HaliConfig &cfg,
                          Tui &tui, Agent &agent);
void slash_lint(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui);
void slash_plan(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui);
void slash_task(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui);

#endif