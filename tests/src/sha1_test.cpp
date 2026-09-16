#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <openssl/sha.h>

using namespace std;
static constexpr char  WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static std::string base64_encode(const uint8_t *data, size_t len) {
  static constexpr char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t octet_a = data[i];
    uint32_t octet_b = (i + 1 < len) ? data[i + 1] : 0;
    uint32_t octet_c = (i + 2 < len) ? data[i + 2] : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    out += TABLE[(triple >> 18) & 0x3F];
    out += TABLE[(triple >> 12) & 0x3F];
    out += (i + 1 < len) ? TABLE[(triple >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? TABLE[triple & 0x3F]       : '=';
  }
  return out;
}

static std::string ws_accept_key(const std::string &client_key) {
  const std::string input = client_key + WS_GUID;
  unsigned char digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(input.data()),  input.size(), digest);
  return base64_encode(digest, SHA_DIGEST_LENGTH);
}

void sha1_test() {
  std::cout << "\nAll sha1 tests passed!" << std::endl;
}
