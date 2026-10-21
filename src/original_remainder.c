/* SPDX-License-Identifier: MIT */
#include "original_remainder.h"

#define JBIT UINT64_C(0x8000000000000000)

original_remainder_status original_remainder(i8087_f80 a, i8087_f80 b,
                                             original_remainder_result *out) {
    int32_t exponent = a.sign_exponent & 0x7fff;
    int32_t distance = exponent - (int32_t)(b.sign_exponent & 0x7fff);
    uint64_t remainder = a.significand, divisor = b.significand, quotient = 0;
    unsigned count = distance < 0 ? 0u : distance >= 64 ? 63u : (unsigned)distance;
    unsigned i;
    *out = (original_remainder_result){
        {0, (uint16_t)(a.sign_exponent & 0x8000)}, 0, 1, distance >= 64, 0};
    if (distance >= 0) {
        /* One same-position trial followed by count shifted trials computes
           divmod(S_A * 2^count, S_B). A normalized divisor bounds S_A < 2*S_B. */
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient = 1;
        }
        for (i = 0; i < count; ++i) {
            bool carry = (remainder & JBIT) != 0;
            bool subtract;
            remainder <<= 1;
            subtract = carry || remainder >= divisor;
            if (subtract)
                remainder -= divisor;
            quotient = (quotient << 1) | (subtract ? 1u : 0u);
        }
        exponent -= (int32_t)count;
    }
    out->quotient = (uint8_t)(quotient & 7);
    out->width = quotient < 2 ? 1u : quotient < 4 ? 2u : 3u;
    if (remainder) {
        while (!(remainder & JBIT)) {
            remainder <<= 1;
            --exponent;
        }
        out->value.significand = remainder;
        out->exponent = exponent;
        if (exponent < 1)
            return ORIGINAL_REMAINDER_RANGE;
        out->value.sign_exponent |= (uint16_t)exponent;
    }
    return ORIGINAL_REMAINDER_COMPLETE;
}
