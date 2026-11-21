// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef CURL_H
#define CURL_H

#include <curl/curl.h>
#include <functional>
#include <string>
#include <cstdint>

// Progress callback: (bytes_downloaded, total_bytes). total is -1 if unknown.
using ProgressCb = std::function<void(int64_t current, int64_t total)>;

void curl_init();
void curl_close();
void curl_set_opts(CURL *curl, long timeoutSecs);
std::string tool_curl(const std::string &url);
std::string tool_curl_download(const std::string &url, const std::string &output_path,
                               ProgressCb progress = nullptr);

#endif