/* SPDX-License-Identifier: MIT */
#include "original_add.h"

#define JBIT UINT64_C(0x8000000000000000)

typedef struct fraction_grs {
    uint64_t fraction;
    unsigned grs;
} fraction_grs;

static fraction_grs align_fraction(uint64_t fraction, unsigned distance) {
    fraction_grs value = {fraction, 0};
    unsigned i;
    /* Each shift retains guard/round and jams the remaining tail. Large
       nonzero shifts are rejected before this helper, pending J20 decoding. */
    for (i = 0; i < distance && (value.fraction || value.grs); ++i) {
        value.grs = (unsigned)((value.fraction & 1) << 2) | (value.grs >> 1) | (value.grs & 1);
        value.fraction >>= 1;
    }
    return value;
}

static void shift_left(fraction_grs *value) {
    value->fraction = (value->fraction << 1) | (value->grs >> 2);
    value->grs = ((value->grs << 1) & 7) | (value->grs & 1);
}

static void normalize_difference(fraction_grs *value, int32_t *exponent) {
    while (!(value->fraction & JBIT)) {
        shift_left(value);
        --*exponent;
    }
}

static bool round_fraction(fraction_grs *value, unsigned precision, unsigned rc, bool sign,
                           bool *increment, bool *inexact) {
    unsigned discard = 64 - precision;
    uint64_t unit = UINT64_C(1) << discard;
    uint64_t residue = ((value->fraction & (unit - 1)) << 3) | value->grs;
    uint64_t half = unit << 2;
    bool carry;
    *increment = false;
    *inexact = residue != 0;
    if (rc == 0)
        *increment = residue > half || (residue == half && (value->fraction & unit));
    else if (rc == 1)
        *increment = residue && sign;
    else if (rc == 2)
        *increment = residue && !sign;
    value->fraction &= ~(unit - 1);
    carry = *increment && value->fraction > UINT64_MAX - unit;
    if (*increment)
        value->fraction += unit;
    if (carry)
        value->fraction = JBIT;
    value->grs = 0;
    return carry;
}

original_add_status original_add(i8087_f80 a, i8087_f80 b, unsigned precision, unsigned rc,
                                 sf_result *out) {
    fraction_grs left, right, value;
    unsigned exponent_a = a.sign_exponent & 0x7fff;
    unsigned exponent_b = b.sign_exponent & 0x7fff;
    unsigned distance;
    int32_t exponent;
    bool sign, inexact, increment, rounding_carry;
    if (exponent_a < exponent_b) {
        i8087_f80 temporary = a;
        unsigned temporary_exponent = exponent_a;
        a = b;
        b = temporary;
        exponent_a = exponent_b;
        exponent_b = temporary_exponent;
    }
    *out = (sf_result){{0, 0}, 0, false, {0}};
    distance = exponent_a - exponent_b;
    /* J20's exponent-dependent rounding(H) circuit needs a separate derivation
       for these counts. Do not replace it with an assumed generic jam shift. */
    if (distance >= 64 && b.significand)
        return ORIGINAL_ADD_ALIGNMENT;
    left = (fraction_grs){a.significand, 0};
    right = align_fraction(b.significand, distance);
    exponent = (int32_t)exponent_a;
    sign = sf_sign(a);
    if (sign == sf_sign(b)) {
        bool carry;
        value.fraction = left.fraction + right.fraction;
        value.grs = right.grs;
        carry = value.fraction < left.fraction;
        if (carry) {
            value.grs = (unsigned)((value.fraction & 1) << 2) | (value.grs >> 1) | (value.grs & 1);
            value.fraction = JBIT | (value.fraction >> 1);
            ++exponent;
        }
    } else {
        uint64_t aligned_fraction = right.fraction;
        if (left.fraction < right.fraction ||
            (left.fraction == right.fraction && left.grs < right.grs)) {
            fraction_grs temporary = left;
            left = right;
            right = temporary;
            sign = !sign;
        }
        value.fraction = left.fraction - right.fraction - (left.grs < right.grs);
        value.grs = (left.grs - right.grs) & 7;
        if (!value.fraction && !value.grs) {
            /* J2 returns before J11 here. Preserve the predicted raw zero but
               leave its C1 behavior guarded until implicit writes are known. */
            out->value.sign_exponent =
                (uint16_t)((a.significand & JBIT ? 0 : exponent_a) | (rc == 1 ? 0x8000 : 0));
            return ORIGINAL_ADD_CANCELLATION_CC;
        }
        if (!value.fraction) {
            if (a.significand & JBIT) {
                /* The reachable normal-A case has exactly the half-bit left.
                   #0280/#0281 promote it before leading-zero normalization. */
                value = (fraction_grs){1, 0};
                --exponent;
                normalize_difference(&value, &exponent);
            }
        } else if ((a.significand | aligned_fraction) & JBIT)
            normalize_difference(&value, &exponent);
    }
    /* J11 resets the fraction to JBIT and increments expAdj on a rounding
       carry. Record the carry separately from a valid unnormal-to-JBIT step. */
    rounding_carry = round_fraction(&value, precision, rc, sign, &increment, &inexact);
    if (rounding_carry)
        ++exponent;
    if (exponent < 1 || exponent > 0x7ffe)
        return ORIGINAL_ADD_RANGE;
    out->value = (i8087_f80){value.fraction, (uint16_t)(exponent | (sign ? 0x8000 : 0))};
    out->flags = inexact ? I8087_PE : 0;
    out->rounded_up = increment;
    return ORIGINAL_ADD_COMPLETE;
}
