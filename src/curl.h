// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <curl/curl.h>
#include <string>

void curl_init();
void curl_close();
void curl_set_opts(CURL *curl);
std::string tool_curl(const std::string &url);
