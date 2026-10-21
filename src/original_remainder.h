/* SPDX-License-Identifier: MIT */
#ifndef I8087_ORIGINAL_REMAINDER_H
#define I8087_ORIGINAL_REMAINDER_H

#include "i8087.h"

typedef enum original_remainder_status {
    ORIGINAL_REMAINDER_COMPLETE,
    ORIGINAL_REMAINDER_RANGE
} original_remainder_status;

typedef struct original_remainder_result {
    i8087_f80 value;
    uint8_t quotient;
    unsigned width;
    bool partial;
    int32_t exponent;
} original_remainder_result;

/* Correctly tagged finite VALID dividend and normal divisor. RANGE leaves
   tiny-result completion to the caller, retaining its normalized fraction
   and signed biased exponent. PC and RC do not affect this path. */
original_remainder_status original_remainder(i8087_f80 a, i8087_f80 b,
                                             original_remainder_result *out);

#endif
