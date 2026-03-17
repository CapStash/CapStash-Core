// src/crypto/whirlpool_impl.c
// Implementation of the thin wrapper functions declared in whirlpool_impl.h.

#include "crypto/whirlpool_impl.h"
#include <stdlib.h>
#include <string.h>

whirlpool_ctx_impl* whirlpool_create(void)
{
    whirlpool_ctx_impl* ctx = (whirlpool_ctx_impl*)malloc(sizeof(whirlpool_ctx_impl));
    if (ctx != NULL) {
        sph_whirlpool_init(ctx);
    }
    return ctx;
}

void whirlpool_free(whirlpool_ctx_impl* ctx)
{
    if (ctx != NULL) {
        // Optional: wipe state before free for paranoia
        memset(ctx, 0, sizeof(*ctx));
        free(ctx);
    }
}

void whirlpool_init(whirlpool_ctx_impl* ctx)
{
    if (ctx != NULL) {
        sph_whirlpool_init(ctx);
    }
}

void whirlpool_update(whirlpool_ctx_impl* ctx, const unsigned char* data, size_t len)
{
    if (ctx != NULL && data != NULL && len > 0) {
        sph_whirlpool(ctx, data, len);
    }
}

void whirlpool_final(whirlpool_ctx_impl* ctx, unsigned char out[64])
{
    if (ctx != NULL && out != NULL) {
        sph_whirlpool_close(ctx, out);
    }
}
