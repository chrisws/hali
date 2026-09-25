// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "curl.h"

//
// TOOL:CURL
//
static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total = size * nmemb;
  std::string *body = static_cast<std::string*>(userdata);
  body->append(ptr, total);
  return total;
}

//
// html_to_text — strip HTML for cleaner TOOL:CURL context
//
// Lightweight HTML→plain-text conversion:
//   • Drops <head>, <script>, <style> blocks entirely.
//   • Inserts newlines at block-level tags (p, div, br, li, h1-h6 …).
//   • Strips all remaining tags.
//   • Decodes common named & numeric HTML entities.
//   • Collapses whitespace runs; caps consecutive blank lines at 2.
//
static std::string html_to_text(const std::string &html) {
  std::string s = html;

  // 1. Remove <head>…</head>
  {
    std::string lo = s;
    std::ranges::transform(lo, lo.begin(), ::tolower);
    auto p0 = lo.find("<head");
    auto p1 = lo.find("</head>");
    if (p0 != std::string::npos && p1 != std::string::npos)
      s.erase(p0, p1 + 7 - p0);
  }

  // 2. Remove <script>…</script> and <style>…</style>
  for (const std::string &tag : {"script", "style"}) {
    std::string open  = "<" + tag;
    std::string close = "</" + tag + ">";
    std::string lo = s;
    std::ranges::transform(lo, lo.begin(), ::tolower);
    for (;;) {
      auto p0 = lo.find(open);
      if (p0 == std::string::npos) break;
      auto p1 = lo.find(close, p0);
      if (p1 == std::string::npos) { s.erase(p0); lo.erase(p0); break; }
      s.erase(p0, p1 + close.size() - p0);
      lo.erase(p0, p1 + close.size() - p0);
    }
  }

  // 3. Replace block-level tags with '\n' before stripping all tags.
  static const char *const BLOCK[] = {
    "p","div","br","li","tr","h1","h2","h3","h4","h5","h6",
    "article","section","header","footer","nav","main", nullptr
  };
  {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
      if (s[i] != '<') { out += s[i++]; continue; }
      auto ce = s.find('>', i);
      if (ce == std::string::npos) { out += s[i++]; continue; }
      std::string inner = s.substr(i + 1, ce - i - 1);
      size_t sp = inner.find_first_of(" \t/\r\n");
      std::string name = (sp != std::string::npos) ? inner.substr(0, sp) : inner;
      std::ranges::transform(name, name.begin(), ::tolower);
      for (int k = 0; BLOCK[k]; ++k) {
        if (name == BLOCK[k]) {
          out += '\n'; break;
        }
      }
      i = ce + 1;
    }
    s = out;
  }

  // 4. Strip all remaining tags.
  {
    std::string out; out.reserve(s.size());
    bool in_tag = false;
    for (char c : s) {
      if (c == '<')  { in_tag = true;  continue; }
      if (c == '>')  { in_tag = false; continue; }
      if (!in_tag)     out += c;
    }
    s = out;
  }

  // 5. Decode common HTML entities.
  static const std::pair<const char*, const char*> ENT[] = {
    {"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},
    {"&apos;","'"},{"&nbsp;"," "},{"&mdash;","—"},{"&ndash;","–"},
    {"&hellip;","…"},{"&#39;","'"},{"&#34;","\""},
    {nullptr,nullptr}
  };
  for (int k = 0; ENT[k].first; ++k) {
    std::string e = ENT[k].first, r = ENT[k].second;
    size_t pos = 0;
    while ((pos = s.find(e, pos)) != std::string::npos)
      { s.replace(pos, e.size(), r); pos += r.size(); }
  }
  // Numeric entities &#NNN; and &#xHHH;
  {
    std::string out; out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
      if (s[i]=='&' && i+2<s.size() && s[i+1]=='#') {
        size_t semi = s.find(';', i+2);
        if (semi != std::string::npos && semi-i < 10) {
          std::string num = s.substr(i+2, semi-i-2);
          try {
            uint32_t cp = (num[0]=='x'||num[0]=='X')
              ? (uint32_t)std::stoul(num.substr(1),nullptr,16)
              : (uint32_t)std::stoul(num);
            if      (cp < 0x80)  { out += (char)cp; }
            else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
            else                 { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
            i = semi+1; continue;
          } catch (...) {}
        }
      }
      out += s[i++];
    }
    s = out;
  }

  // 6. Collapse whitespace; cap blank lines at 2.
  {
    std::string out; out.reserve(s.size());
    int nl_run = 0; bool last_sp = false;
    for (char c : s) {
      if (c == '\r') continue;
      if (c == '\t') c = ' ';
      if (c == '\n') { ++nl_run; last_sp=false; if (nl_run<=2) out+='\n'; continue; }
      nl_run = 0;
      if (c == ' ') { if (!last_sp) { out+=' '; last_sp=true; } continue; }
      last_sp = false; out += c;
    }
    size_t f = out.find_first_not_of(" \n");
    size_t l = out.find_last_not_of(" \n");
    s = (f == std::string::npos) ? "" : out.substr(f, l-f+1);
  }
  return s;
}

std::string tool_curl(const std::string &url) {
  if (url.empty()) return "ERROR: TOOL:CURL requires a URL argument";
  CURL *curl = curl_easy_init();
  if (!curl) return "ERROR: curl_easy_init failed";
  std::string body;
  body.reserve(4096);
  curl_set_opts(curl);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  // Query content-type before cleanup (pointer is only valid while handle lives).
  char *ct_raw = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct_raw);
  std::string content_type = ct_raw ? ct_raw : "";
  std::ranges::transform(content_type, content_type.begin(), ::tolower);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK) {
    return std::string("ERROR: curl: ") + curl_easy_strerror(res);
  }
  if (http_code >= 400) {
    return "ERROR: HTTP " + std::to_string(http_code) + " from " + url;
  }
  if (body.empty()) {
    return "(empty response)";
  }

  // Strip HTML tags so the model receives clean plain text.
  const bool is_html = (content_type.find("text/html") != std::string::npos)
    || (body.size() > 5 && body.substr(0,5) == "<!DOC")
    || (body.size() > 6 && body.substr(0,6) == "<html>");
  if (is_html) {
    body = html_to_text(body);
  }

  return body;
}

void curl_init() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

void curl_close() {
  curl_global_cleanup();
}

void curl_set_opts(CURL *curl) {
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,      "hali/1.0");
  // Accept compressed responses; curl will decompress automatically.
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
}
