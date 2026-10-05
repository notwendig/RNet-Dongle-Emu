/*
 * rnd-decrypt.c
 *
 * Decrypts PG Drives Technology R-net Programmer .rnd database files
 * used by the examined RNet Programmer6 OEM.exe (2018 build).
 *
 * The Programmer uses standard Blowfish with:
 *   - a deterministic 31-byte key generated from its internal PRNG
 *   - ECB-style independent 8-byte blocks
 *   - each 32-bit half byte-swapped before and after Blowfish
 *
 * Build on Fedora:
 *   sudo dnf install gcc openssl-devel
 *   gcc -O2 -Wall -Wextra -Wno-deprecated-declarations \
 *       -o rnd-decrypt rnd-decrypt.c -lcrypto
 *
 * Usage:
 *   ./rnd-decrypt Generic_V33_1_1245.rnd Generic_V33_1_1245.dec.bin
 */

#include <openssl/blowfish.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RNG_N = 351,
    RNG_M = 175,
    KEY_LEN = 31
};

#define RNG_LOWER_MASK UINT32_C(0x0007ffff)
#define RNG_TWIST      UINT32_C(0xe4bd75f5)
#define RNG_SEED       UINT32_C(0x672d83e1)
#define RNG_LCG_MUL    UINT32_C(0x01c8e815)

struct pgdt_rng {
    uint32_t state[RNG_N];
    unsigned index;
};

static void rng_init(struct pgdt_rng *r)
{
    uint32_t x = RNG_SEED;
    unsigned i;

    for (i = 0; i < RNG_N; ++i) {
        x = x * RNG_LCG_MUL - UINT32_C(1);
        r->state[i] = x;
    }

    r->index = RNG_N;
}

static void rng_twist(struct pgdt_rng *r)
{
    unsigned i;

    /*
     * The original implementation updates the state in place.
     * For wrapped references this intentionally reads values that
     * have already been updated during the current twist.
     */
    for (i = 0; i < RNG_N - 1; ++i) {
        uint32_t y =
            (r->state[i] & ~RNG_LOWER_MASK) |
            (r->state[i + 1] & RNG_LOWER_MASK);

        r->state[i] =
            r->state[(i + RNG_M) % RNG_N] ^
            (y >> 1) ^
            ((y & 1U) ? RNG_TWIST : 0U);
    }

    {
        uint32_t y =
            (r->state[RNG_N - 1] & ~RNG_LOWER_MASK) |
            (r->state[0] & RNG_LOWER_MASK);

        r->state[RNG_N - 1] =
            r->state[(RNG_N - 1 + RNG_M) % RNG_N] ^
            (y >> 1) ^
            ((y & 1U) ? RNG_TWIST : 0U);
    }

    r->index = 0;
}

static uint32_t rng_next(struct pgdt_rng *r)
{
    uint32_t x;

    if (r->index >= RNG_N)
        rng_twist(r);

    x = r->state[r->index++];

    x ^= x >> 11;
    x ^= (x & UINT32_C(0x00cabca5)) << 7;
    x ^= (x & UINT32_C(0xffffffab)) << 15;
    x ^= x >> 17;

    return x;
}

static void make_key(unsigned char key[KEY_LEN])
{
    struct pgdt_rng r;
    unsigned i;

    rng_init(&r);

    for (i = 0; i < KEY_LEN; ++i) {
        uint32_t x = rng_next(&r);

        /*
         * Equivalent to the Programmer's floating-point sequence:
         *   1 + trunc(254 * (x / 2^32))
         *
         * The integer form is exact for this purpose.
         */
        key[i] = (unsigned char)
            (1U + (unsigned)(((uint64_t)254U * x) >> 32));
    }
}

static void swap_u32_bytes(const unsigned char in[8], unsigned char out[8])
{
    unsigned w, j;

    for (w = 0; w < 2; ++w) {
        for (j = 0; j < 4; ++j)
            out[w * 4 + j] = in[w * 4 + (3U - j)];
    }
}

static void print_key(const unsigned char key[KEY_LEN])
{
    unsigned i;

    fputs("Blowfish key: ", stdout);
    for (i = 0; i < KEY_LEN; ++i)
        printf("%02X", key[i]);
    putchar('\n');
}

int main(int argc, char **argv)
{
    FILE *fin = NULL;
    FILE *fout = NULL;
    BF_KEY bf;
    unsigned char key[KEY_LEN];
    unsigned char in[8], swapped_in[8];
    unsigned char swapped_out[8], out[8];
    size_t n;
    uint64_t blocks = 0;
    int rc = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s INPUT.rnd OUTPUT.bin\n",
                argc > 0 ? argv[0] : "rnd-decrypt");
        return EXIT_FAILURE;
    }

    make_key(key);
    print_key(key);

    fin = fopen(argv[1], "rb");
    if (!fin) {
        fprintf(stderr, "Cannot open input '%s': %s\n",
                argv[1], strerror(errno));
        goto done;
    }

    fout = fopen(argv[2], "wb");
    if (!fout) {
        fprintf(stderr, "Cannot create output '%s': %s\n",
                argv[2], strerror(errno));
        goto done;
    }

    BF_set_key(&bf, KEY_LEN, key);

    while ((n = fread(in, 1, sizeof(in), fin)) == sizeof(in)) {
        swap_u32_bytes(in, swapped_in);

        BF_ecb_encrypt(swapped_in, swapped_out, &bf, BF_DECRYPT);

        swap_u32_bytes(swapped_out, out);

        if (fwrite(out, 1, sizeof(out), fout) != sizeof(out)) {
            fprintf(stderr, "Write error: %s\n", strerror(errno));
            goto done;
        }

        ++blocks;
    }

    if (ferror(fin)) {
        fprintf(stderr, "Read error: %s\n", strerror(errno));
        goto done;
    }

    if (n != 0) {
        fprintf(stderr,
                "Input size is not a multiple of 8 bytes "
                "(trailing bytes: %zu).\n",
                n);
        goto done;
    }

    if (fflush(fout) != 0) {
        fprintf(stderr, "Flush error: %s\n", strerror(errno));
        goto done;
    }

    printf("Decrypted %llu blocks (%llu bytes).\n",
           (unsigned long long)blocks,
           (unsigned long long)(blocks * 8ULL));
    printf("Output: %s\n", argv[2]);

    rc = EXIT_SUCCESS;

done:
    if (fout)
        fclose(fout);
    if (fin)
        fclose(fin);

    return rc;
}
