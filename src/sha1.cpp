// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cstring>

#include "sha1.h"

struct Context {
  uint32_t h[5];
  uint8_t  buffer[64];
  uint64_t total_bytes;
  size_t   buffer_len;
};

static inline uint32_t rotl(uint32_t v, int n) {
  return (v << n) | (v >> (32 - n));
}

static void init(Context &ctx) {
  ctx.h[0] = 0x67452301u;
  ctx.h[1] = 0xEFCDAB89u;
  ctx.h[2] = 0x98BADCFEu;
  ctx.h[3] = 0x10325476u;
  ctx.h[4] = 0xC3D2E1F0u;
  ctx.total_bytes = 0;
  ctx.buffer_len  = 0;
}

static void process_block(const uint8_t *block, uint32_t h[5]) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(block[i * 4])     << 24)
      | (uint32_t(block[i * 4 + 1]) << 16)
      | (uint32_t(block[i * 4 + 2]) << 8)
      | (uint32_t(block[i * 4 + 3]));
  }
  for (int i = 16; i < 80; ++i) {
    w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
    else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
    else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
    uint32_t temp = rotl(a, 5) + f + e + k + w[i];
    e = d;  d = c;  c = rotl(b, 30);  b = a;  a = temp;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void update(Context &ctx, const uint8_t *data, size_t len) {
  ctx.total_bytes += len;
  while (len > 0) {
    size_t space = 64 - ctx.buffer_len;
    size_t copy  = (len < space) ? len : space;
    std::memcpy(ctx.buffer + ctx.buffer_len, data, copy);
    ctx.buffer_len += copy;
    data           += copy;
    len            -= copy;
    if (ctx.buffer_len == 64) {
      process_block(ctx.buffer, ctx.h);
      ctx.buffer_len = 0;
    }
  }
}

static void final(Context &ctx, uint8_t digest[20]) {
  // Append padding: 0x80, then zeros, then 64-bit big-endian bit length.
  uint64_t bit_len = ctx.total_bytes * 8;
  uint8_t  pad     = 0x80;
  update(ctx, &pad, 1);
  uint8_t zero = 0;
  while (ctx.buffer_len != 56) {
    update(ctx, &zero, 1);
  }
  uint8_t len_bytes[8];
  for (int i = 0; i < 8; ++i) {
    len_bytes[i] = uint8_t((bit_len >> (8 * (7 - i))) & 0xFF);
  }
  update(ctx, len_bytes, 8);

  for (int i = 0; i < 5; ++i) {
    digest[i * 4]     = uint8_t((ctx.h[i] >> 24) & 0xFF);
    digest[i * 4 + 1] = uint8_t((ctx.h[i] >> 16) & 0xFF);
    digest[i * 4 + 2] = uint8_t((ctx.h[i] >> 8)  & 0xFF);
    digest[i * 4 + 3] = uint8_t(ctx.h[i] & 0xFF);
  }
}

namespace sha1 {
  std::array<uint8_t, 20> hash(const std::string &input) {
    Context ctx;
    init(ctx);
    update(ctx, reinterpret_cast<const uint8_t *>(input.data()), input.size());
    std::array<uint8_t, 20> digest{};
    final(ctx, digest.data());
    return digest;
  }
}
