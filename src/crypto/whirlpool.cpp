#include <crypto/whirlpool.h>

// Pull in SPHlib headers with C linkage so symbols match sph_whirlpool.c
extern "C" {
#include <crypto/whirlpool/sph_whirlpool.h>
}

#include <cstring>

// Our private impl: just wraps SPHlib context.
struct CWhirlpool512::Impl {
    sph_whirlpool_context ctx;
};

CWhirlpool512::CWhirlpool512()
{
    p = new Impl();
    sph_whirlpool_init(&p->ctx);
}

CWhirlpool512::~CWhirlpool512()
{
    if (p) {
        std::memset(&p->ctx, 0, sizeof(p->ctx));
        delete p;
        p = nullptr;
    }
}

CWhirlpool512& CWhirlpool512::Write(const unsigned char* data, size_t len)
{
    if (data != nullptr && len) {
        sph_whirlpool(&p->ctx, data, len);
    }
    return *this;
}

void CWhirlpool512::Finalize(unsigned char hash[WHIRLPOOL512_OUTPUT_SIZE])
{
    sph_whirlpool_close(&p->ctx, hash);
    sph_whirlpool_init(&p->ctx); // re-init for reuse
}

CWhirlpool512& CWhirlpool512::Reset()
{
    sph_whirlpool_init(&p->ctx);
    return *this;
}

void Whirlpool512(const unsigned char* data, size_t len,
                  unsigned char out[WHIRLPOOL512_OUTPUT_SIZE])
{
    CWhirlpool512 h;
    h.Write(data, len);
    h.Finalize(out);
}
