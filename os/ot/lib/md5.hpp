#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Simple MD5 implementation for integrity verification.
 * Based on RFC 1321.
 */

struct MD5Context {
  uint32_t state[4];  // ABCD
  uint32_t count[2];  // number of bits, mod 2^64
  uint8_t buffer[64]; // input buffer
};

void md5_init(MD5Context *ctx);
void md5_update(MD5Context *ctx, const uint8_t *input, size_t len);
void md5_final(MD5Context *ctx, uint8_t digest[16]);

// Helper to format digest as hex string (needs 33 bytes buffer)
void md5_digest_to_hex(const uint8_t digest[16], char hex[33]);
