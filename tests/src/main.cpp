void file_test();
void graph_test();
void string_utils_test();
void mcp_format_test();
void sha1_test();

#include "logging.h"

int main() {
  log_open_console();
  file_test();
  graph_test();
  string_utils_test();
  mcp_format_test();
  sha1_test();
  return 0;
}
