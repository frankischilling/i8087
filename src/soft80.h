/* SPDX-License-Identifier: MIT */
#ifndef I8087_SOFT80_H
#define I8087_SOFT80_H
#include "i8087.h"

enum sf_kind { SF_ZERO, SF_FINITE, SF_INFINITY, SF_NAN };

/* The unbounded rounded value is retained separately from the canonical
   IEEE result. Original-8087 unmasked range exceptions need this value with
   exponent +/- 24576, NOT a rounded subnormal/infinity reconstructed later. */
typedef struct sf_unbounded {
    uint64_t significand;
    int32_t exponent, exact_exponent;
    uint16_t flags;
    bool rounded_up, sign, valid;
} sf_unbounded;

typedef struct sf_result {
    i8087_f80 value;
    uint16_t flags;
    bool rounded_up;
    sf_unbounded unbounded;
} sf_result;

enum sf_kind sf_kind(i8087_f80 x);
bool sf_sign(i8087_f80 x);
bool sf_unnormal(i8087_f80 x);
bool sf_denormal(i8087_f80 x);
unsigned sf_tag(i8087_f80 x);
int32_t sf_exponent(i8087_f80 x);
i8087_f80 sf_normalize(i8087_f80 x);
i8087_f80 sf_zero(bool negative);
i8087_f80 sf_infinity(bool negative);
i8087_f80 sf_indefinite(void);
i8087_f80 sf_from_u64(uint64_t x, bool negative);
i8087_f80 sf_from_signed_bits(uint64_t bits, unsigned width);
/* FLD32/64 preserve denormal significance as an 8087 unnormal. */
i8087_f80 sf_load_ieee(uint64_t bits, unsigned width, uint16_t *flags);
uint64_t sf_store_ieee(i8087_f80 x, unsigned width, unsigned rc, uint16_t *flags, bool *rounded_up);
uint64_t sf_to_integer(i8087_f80 x, unsigned width, unsigned rc, uint16_t *flags, bool *rounded_up);
sf_result sf_round_integer(i8087_f80 x, unsigned rc);
sf_result sf_add(i8087_f80 a, i8087_f80 b, unsigned precision, unsigned rc);
sf_result sf_multiply(i8087_f80 a, i8087_f80 b, unsigned precision, unsigned rc);
sf_result sf_divide(i8087_f80 a, i8087_f80 b, unsigned precision, unsigned rc);
sf_result sf_sqrt(i8087_f80 x, unsigned precision, unsigned rc);
sf_result sf_scale(i8087_f80 x, int32_t delta, unsigned rc);
/* -1,0,1, or 2 for unordered. Normalizes numeric values for comparison. */
int sf_compare(i8087_f80 a, i8087_f80 b);
/* An exact bounded integer remainder step; quotient bits are magnitude bits. */
i8087_f80 sf_remainder(i8087_f80 a, i8087_f80 b, uint8_t *quotient, bool *partial,
                       unsigned *qwidth);
#endif
