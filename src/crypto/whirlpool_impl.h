// src/crypto/whirlpool_impl.h
// Thin wrapper around SPHlib's Whirlpool implementation.

#ifndef CAPSTASH_CRYPTO_WHIRLPOOL_IMPL_H
#define CAPSTASH_CRYPTO_WHIRLPOOL_IMPL_H

#include "crypto/whirlpool/sph_whirlpool.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef sph_whirlpool_context whirlpool_ctx_impl;

whirlpool_ctx_impl* whirlpool_create(void);
void whirlpool_free(whirlpool_ctx_impl* ctx);
void whirlpool_init(whirlpool_ctx_impl* ctx);
void whirlpool_update(whirlpool_ctx_impl* ctx, const unsigned char* data, size_t len);
void whirlpool_final(whirlpool_ctx_impl* ctx, unsigned char out[64]);

#ifdef __cplusplus
}
#endif

#endif // CAPSTASH_CRYPTO_WHIRLPOOL_IMPL_H
