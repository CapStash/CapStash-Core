#ifndef CAPSTASH_CRYPTO_WHIRLPOOL_H
#define CAPSTASH_CRYPTO_WHIRLPOOL_H

#include <cstddef>
#include <cstdint>

static const size_t WHIRLPOOL512_OUTPUT_SIZE = 64; // 512 bits

class CWhirlpool512
{
public:
    CWhirlpool512();
    ~CWhirlpool512();

    CWhirlpool512& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[WHIRLPOOL512_OUTPUT_SIZE]);
    CWhirlpool512& Reset();

private:
    // Directly embed SPHlib context. No malloc, no complex ownership.
    struct Impl;
    Impl* p;
};

void Whirlpool512(const unsigned char* data, size_t len,
                  unsigned char out[WHIRLPOOL512_OUTPUT_SIZE]);

#endif // CAPSTASH_CRYPTO_WHIRLPOOL_H
