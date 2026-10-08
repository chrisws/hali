// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <iostream>
#include <string>

#include "curl.cpp"

using namespace std;

//
// Test: is_blocked_url rejects private / link-local / loopback addresses
//
static void test_blocked_urls() {
  // Loopback
  assert(is_blocked_url("http://127.0.0.1/"));
  assert(is_blocked_url("http://127.0.0.1:8080/path"));
  assert(is_blocked_url("http://localhost/"));
  assert(is_blocked_url("http://localhost:3000/api"));

  // Private 10.x
  assert(is_blocked_url("http://10.0.0.1/"));
  assert(is_blocked_url("http://10.255.255.255/"));

  // Private 172.16-31.x
  assert(is_blocked_url("http://172.16.0.1/"));
  assert(is_blocked_url("http://172.31.255.255/"));

  // Private 192.168.x
  assert(is_blocked_url("http://192.168.1.1/"));
  assert(is_blocked_url("http://192.168.0.1/"));

  // Link-local 169.254.x (cloud metadata)
  assert(is_blocked_url("http://169.254.169.254/latest/meta-data/"));

  // IPv6 loopback
  assert(is_blocked_url("http://[::1]/"));

  cout << "test_blocked_urls passed" << endl;
}

//
// Test: is_blocked_url allows public addresses
//
static void test_allowed_urls() {
  // Public IPs
  assert(!is_blocked_url("http://93.184.216.34/"));
  assert(!is_blocked_url("http://8.8.8.8/"));
  assert(!is_blocked_url("https://1.1.1.1/"));

  // 172.x outside 16-31 range
  assert(!is_blocked_url("http://172.15.0.1/"));
  assert(!is_blocked_url("http://172.32.0.1/"));

  // Domain names (no IP to check)
  assert(!is_blocked_url("https://example.com/"));
  assert(!is_blocked_url("https://github.com/user/repo"));

  // No scheme (should not block)
  assert(!is_blocked_url("example.com"));

  cout << "test_allowed_urls passed" << endl;
}

//
// Test: tool_curl with empty URL
//
static void test_curl_empty_url() {
  string result = tool_curl("");
  assert(result.find("ERROR") != string::npos);
  assert(result.find("requires a URL") != string::npos);

  cout << "test_curl_empty_url passed" << endl;
}

//
// Test: tool_curl with blocked URL (should return error, no HTTP request)
//
static void test_curl_blocked_url() {
  string result = tool_curl("http://169.254.169.254/latest/meta-data/");
  assert(result.find("ERROR") != string::npos);
  assert(result.find("blocked") != string::npos);

  result = tool_curl("http://127.0.0.1/");
  assert(result.find("ERROR") != string::npos);
  assert(result.find("blocked") != string::npos);

  result = tool_curl("http://localhost/");
  assert(result.find("ERROR") != string::npos);
  assert(result.find("blocked") != string::npos);

  cout << "test_curl_blocked_url passed" << endl;
}

//
// Test: html_to_text strips HTML tags
//
static void test_html_to_text() {
  string html = "<html><head><title>Test</title></head><body><p>Hello World</p></body></html>";
  string text = html_to_text(html);
  assert(text.find("Hello World") != string::npos);
  assert(text.find("<p>") == string::npos);
  assert(text.find("<html>") == string::npos);
  assert(text.find("<head>") == string::npos);

  // Script and style blocks removed
  string html2 = "<div>Before</div><script>var x=1;</script><div>After</div>";
  string text2 = html_to_text(html2);
  assert(text2.find("Before") != string::npos);
  assert(text2.find("After") != string::npos);
  assert(text2.find("var x=1") == string::npos);

  // HTML entities decoded
  string html3 = "Tom &amp; Jerry &lt;3";
  string text3 = html_to_text(html3);
  assert(text3.find("Tom & Jerry") != string::npos);
  assert(text3.find("<3") != string::npos);

  cout << "test_html_to_text passed" << endl;
}

void curl_test() {
  test_blocked_urls();
  test_allowed_urls();
  test_curl_empty_url();
  test_curl_blocked_url();
  test_html_to_text();
  cout << "\nAll CURL tests passed!\n" << endl;
}
