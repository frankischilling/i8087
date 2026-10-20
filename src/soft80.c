/* SPDX-License-Identifier: MIT
 * Integer-only binary80 arithmetic. No host FP, libm, inline assembly, global
 * rounding state, compiler 128-bit extension, or external runtime dependency.
 * This backend's canonical arithmetic is separately tested; it is NOT a model
 * of the 8087's unnormal-propagation or exception-restart microcode.
 */
#include "soft80.h"
#include <limits.h>

#define JBIT UINT64_C(0x8000000000000000)
#define EXP_MASK 0x7fffu
#define BIAS 16383

typedef struct wide {
    uint64_t hi, lo;
} wide;

typedef struct unpacked {
    uint64_t sig;
    int32_t exp;
    bool sign;
} unpacked;

typedef struct rounded {
    uint64_t sig;
    int32_t exp;
    uint16_t flags;
    bool up, infinity;
} rounded;

static wide w64(uint64_t x)
{
    wide r = {0, x};
    return r;
}

static bool wnz(wide x)
{
    return x.hi != 0 || x.lo != 0;
}

static int wcmp(wide a, wide b)
{
    if (a.hi != b.hi)
        return a.hi > b.hi ? 1 : -1;
    return a.lo == b.lo ? 0 : (a.lo > b.lo ? 1 : -1);
}

static wide wadd(wide a, wide b)
{
    wide r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo);
    return r;
}

static wide wsub(wide a, wide b)
{
    wide r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo);
    return r;
}

static wide wleft(wide x, unsigned n)
{
    wide r;
    if (!n)
        return x;
    if (n >= 128)
        return w64(0);
    if (n >= 64) {
        r.hi = x.lo << (n - 64);
        r.lo = 0;
        return r;
    }
    r.hi = (x.hi << n) | (x.lo >> (64 - n));
    r.lo = x.lo << n;
    return r;
}

static wide wright(wide x, unsigned n)
{
    wide r;
    if (!n)
        return x;
    if (n >= 128)
        return w64(0);
    if (n >= 64) {
        r.lo = x.hi >> (n - 64);
        r.hi = 0;
        return r;
    }
    r.lo = (x.lo >> n) | (x.hi << (64 - n));
    r.hi = x.hi >> n;
    return r;
}

static wide wjam(wide x, unsigned n)
{
    wide r = wright(x, n);
    if (n && wcmp(wleft(r, n), x))
        r.lo |= 1;
    return r;
}

static unsigned wbits(wide x)
{
    unsigned n = 0;
    uint64_t a = x.hi ? x.hi : x.lo;
    while (a) {
        ++n;
        a >>= 1;
    }
    return n + (x.hi ? 64u : 0u);
}

static wide wmul(uint64_t a, uint64_t b)
{
    const uint64_t mask = UINT64_C(0xffffffff);
    uint64_t a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
    uint64_t p0 = a0 * b0, p1 = a0 * b1, p2 = a1 * b0, p3 = a1 * b1;
    uint64_t middle = (p0 >> 32) + (p1 & mask) + (p2 & mask);
    wide r = {p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32), (middle << 32) | (p0 & mask)};
    return r;
}

static unpacked unpack(i8087_f80 x)
{
    unpacked r;
    r.sign = sf_sign(x);
    r.sig = x.significand;
    r.exp = (int32_t)(x.sign_exponent & EXP_MASK);
    r.exp = (r.exp ? r.exp : 1) - BIAS;
    if (r.sig)
        while (!(r.sig & JBIT)) {
            r.sig <<= 1;
            --r.exp;
        }
    return r;
}

enum sf_kind sf_kind(i8087_f80 x)
{
    unsigned e = x.sign_exponent & EXP_MASK;
    if (e == EXP_MASK)
        return x.significand == JBIT ? SF_INFINITY : SF_NAN;
    return x.significand ? SF_FINITE : SF_ZERO;
}

bool sf_sign(i8087_f80 x)
{
    return (x.sign_exponent & 0x8000u) != 0;
}

bool sf_unnormal(i8087_f80 x)
{
    unsigned e = x.sign_exponent & EXP_MASK;
    return e && e != EXP_MASK && !(x.significand & JBIT);
}

bool sf_denormal(i8087_f80 x)
{
    return !(x.sign_exponent & EXP_MASK) && x.significand != 0;
}

unsigned sf_tag(i8087_f80 x)
{
    /* A zero significand with a nonzero finite exponent is an original-8087
       pseudozero: an unnormal tagged VALID, not a canonical zero. */
    if (sf_unnormal(x))
        return I8087_TAG_VALID;
    if (sf_kind(x) == SF_ZERO)
        return I8087_TAG_ZERO;
    if (sf_kind(x) != SF_FINITE || sf_denormal(x))
        return I8087_TAG_SPECIAL;
    return I8087_TAG_VALID; /* 8087 unnormals are VALID, not SPECIAL. */
}

i8087_f80 sf_zero(bool sign)
{
    i8087_f80 r = {0, (uint16_t)(sign ? 0x8000 : 0)};
    return r;
}

i8087_f80 sf_infinity(bool sign)
{
    i8087_f80 r = {JBIT, (uint16_t)(0x7fff | (sign ? 0x8000 : 0))};
    return r;
}

i8087_f80 sf_indefinite(void)
{
    i8087_f80 r = {UINT64_C(0xc000000000000000), 0xffff};
    return r;
}

int32_t sf_exponent(i8087_f80 x)
{
    return unpack(x).exp;
}

i8087_f80 sf_normalize(i8087_f80 x)
{
    unpacked u = unpack(x);
    if (sf_kind(x) == SF_ZERO)
        return sf_zero(u.sign);
    if (sf_kind(x) != SF_FINITE)
        return x;
    if (u.exp < -16382) {
        unsigned n = (unsigned)(-16382 - u.exp);
        x.significand = n < 64 ? u.sig >> n : 0;
        x.sign_exponent = (uint16_t)(u.sign ? 0x8000 : 0);
    } else {
        x.significand = u.sig;
        x.sign_exponent = (uint16_t)(u.exp + BIAS + (u.sign ? 0x8000 : 0));
    }
    return x;
}

/* x has three low rounding bits; normalize its leading bit to position 66. */
static rounded quantize(bool sign, int32_t exp, wide x, unsigned precision, unsigned rc,
                        int32_t emin, int32_t emax)
{
    rounded r = {0, exp, 0, false, false};
    unsigned bits, discard;
    uint64_t residue, half;
    wide q;
    bool tiny, increment = false;
    if (!wnz(x))
        return r;
    bits = wbits(x);
    if (bits > 67) {
        x = wjam(x, bits - 67);
        exp += (int32_t)(bits - 67);
    } else if (bits < 67) {
        x = wleft(x, 67 - bits);
        exp -= (int32_t)(67 - bits);
    }
    tiny = exp < emin;
    if (tiny) {
        x = wjam(x, (unsigned)(emin - exp));
        exp = emin;
    }
    discard = 67 - precision; /* 3, 14, or 43; never 0 or 64. */
    residue = x.lo & ((UINT64_C(1) << discard) - 1);
    half = UINT64_C(1) << (discard - 1);
    q = wright(x, discard);
    if (residue) {
        r.flags |= I8087_PE;
        if (rc == 0)
            increment = residue > half || (residue == half && (q.lo & 1));
        else if (rc == 1)
            increment = sign;
        else if (rc == 2)
            increment = !sign;
    }
    if (increment)
        q = wadd(q, w64(1));
    if (wbits(q) > precision) {
        q = wright(q, 1);
        ++exp;
    }
    r.up = increment;
    if (exp > emax) {
        bool to_inf = rc == 0 || (rc == 1 && sign) || (rc == 2 && !sign);
        r.flags |= I8087_OE | I8087_PE;
        r.infinity = to_inf;
        r.exp = emax;
        r.sig = to_inf ? JBIT : (UINT64_MAX << (64 - precision));
        return r;
    }
    r.sig = q.lo << (64 - precision);
    r.exp = exp;
    if (tiny && (r.flags & I8087_PE) && !(r.sig & JBIT))
        r.flags |= I8087_UE;
    return r;
}

static sf_result finish(bool sign, int32_t e, wide sig, unsigned p, unsigned rc)
{
    rounded q = quantize(sign, e, sig, p, rc, -16382, 16383);
    sf_result r;
    rounded full = quantize(sign, e, sig, p, rc, -100000, 100000);
    r.unbounded.significand = full.sig;
    r.unbounded.exponent = full.exp;
    r.unbounded.exact_exponent = e + (int32_t)wbits(sig) - 67;
    r.unbounded.flags = full.flags;
    r.unbounded.rounded_up = full.up;
    r.unbounded.sign = sign;
    r.unbounded.valid = wnz(sig);
    r.flags = q.flags;
    r.rounded_up = q.up;
    if (q.infinity)
        r.value = sf_infinity(sign);
    else if (!q.sig)
        r.value = sf_zero(sign);
    else {
        r.value.significand = q.sig;
        r.value.sign_exponent =
            (uint16_t)((sign ? 0x8000 : 0) |
                       ((q.exp == -16382 && !(q.sig & JBIT)) ? 0 : (q.exp + BIAS)));
    }
    return r;
}

static sf_result result(i8087_f80 x, uint16_t flags)
{
    sf_result r = {x, flags, false, {0}};
    return r;
}

static sf_result nan_result(i8087_f80 a, i8087_f80 b)
{
    i8087_f80 x = sf_kind(a) == SF_NAN ? a : b;
    if (sf_kind(a) == SF_NAN && sf_kind(b) == SF_NAN && b.significand > a.significand)
        x = b;
    return result(x, I8087_IE); /* Original 8087: all NaN operands signal. */
}

i8087_f80 sf_from_u64(uint64_t x, bool sign)
{
    i8087_f80 r;
    int e = 63;
    if (!x)
        return sf_zero(sign);
    while (!(x & JBIT)) {
        x <<= 1;
        --e;
    }
    r.significand = x;
    r.sign_exponent = (uint16_t)(e + BIAS + (sign ? 0x8000 : 0));
    return r;
}

i8087_f80 sf_from_signed_bits(uint64_t bits, unsigned width)
{
    uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    bool sign = ((bits >> (width - 1)) & 1) != 0;
    bits &= mask;
    return sf_from_u64(sign ? ((~bits + 1) & mask) : bits, sign);
}

i8087_f80 sf_load_ieee(uint64_t bits, unsigned width, uint16_t *flags)
{
    unsigned frac = width == 32 ? 23u : 52u;
    unsigned ebits = width == 32 ? 8u : 11u;
    unsigned bias = (1u << (ebits - 1)) - 1;
    unsigned emask = (1u << ebits) - 1;
    unsigned e = (unsigned)((bits >> frac) & emask);
    uint64_t f = bits & ((UINT64_C(1) << frac) - 1);
    bool sign = ((bits >> (width - 1)) & 1) != 0;
    i8087_f80 r;
    if (!e && !f)
        return sf_zero(sign);
    if (e == emask) {
        if (!f)
            return sf_infinity(sign);
        *flags |= I8087_IE;
        r.significand = JBIT | (f << (63 - frac));
        r.sign_exponent = (uint16_t)(0x7fff | (sign ? 0x8000 : 0));
        return r;
    }
    if (!e)
        *flags |= I8087_DE;
    r.significand = (e ? JBIT : 0) | (f << (63 - frac));
    r.sign_exponent = (uint16_t)((int)(e ? e : 1) - (int)bias + BIAS + (sign ? 0x8000 : 0));
    return r;
}

uint64_t sf_store_ieee(i8087_f80 x, unsigned width, unsigned rc, uint16_t *flags, bool *up)
{
    unsigned frac = width == 32 ? 23u : 52u, bias = width == 32 ? 127u : 1023u;
    uint64_t sign = (uint64_t)sf_sign(x) << (width - 1);
    uint64_t emask = (UINT64_C(1) << (width == 32 ? 8 : 11)) - 1;
    enum sf_kind k = sf_kind(x);
    unpacked u;
    rounded q;
    *up = false;
    if (k == SF_ZERO)
        return sign;
    if (k == SF_INFINITY)
        return sign | (emask << frac);
    if (k == SF_NAN) {
        uint64_t payload = (x.significand >> (63 - frac)) & ((UINT64_C(1) << frac) - 1);
        *flags |= I8087_IE;
        if (!payload)
            payload = UINT64_C(1) << (frac - 1);
        return sign | (emask << frac) | payload;
    }
    u = unpack(x);
    q = quantize(u.sign, u.exp, wleft(w64(u.sig), 3), frac + 1, rc, 1 - (int32_t)bias,
                 (int32_t)bias);
    *flags |= q.flags;
    *up = q.up;
    if (q.infinity)
        return sign | (emask << frac);
    if (!q.sig)
        return sign;
    return sign | ((q.sig & JBIT ? (uint64_t)(q.exp + (int32_t)bias) : 0) << frac) |
           ((q.sig >> (63 - frac)) & ((UINT64_C(1) << frac) - 1));
}

sf_result sf_add(i8087_f80 a, i8087_f80 b, unsigned p, unsigned rc)
{
    enum sf_kind ka = sf_kind(a), kb = sf_kind(b);
    unpacked ua, ub, t;
    wide sa, sb, s;
    bool sign;
    if (ka == SF_NAN || kb == SF_NAN)
        return nan_result(a, b);
    if (ka == SF_INFINITY || kb == SF_INFINITY) {
        if (ka == kb && sf_sign(a) != sf_sign(b))
            return result(sf_indefinite(), I8087_IE);
        return result(ka == SF_INFINITY ? a : b, 0);
    }
    ua = unpack(a);
    ub = unpack(b);
    if (ka == SF_ZERO && kb == SF_ZERO)
        return result(sf_zero(ua.sign == ub.sign ? ua.sign : rc == 1), 0);
    if (ka == SF_ZERO)
        return finish(ub.sign, ub.exp, wleft(w64(ub.sig), 3), p, rc);
    if (kb == SF_ZERO)
        return finish(ua.sign, ua.exp, wleft(w64(ua.sig), 3), p, rc);
    if (ua.exp < ub.exp || (ua.exp == ub.exp && ua.sig < ub.sig)) {
        t = ua;
        ua = ub;
        ub = t;
    }
    sa = wleft(w64(ua.sig), 3);
    sb = wjam(wleft(w64(ub.sig), 3), (unsigned)(ua.exp - ub.exp));
    sign = ua.sign;
    if (ua.sign == ub.sign)
        s = wadd(sa, sb);
    else {
        s = wsub(sa, sb);
        if (!wnz(s))
            sign = rc == 1;
    }
    return finish(sign, ua.exp, s, p, rc);
}

sf_result sf_multiply(i8087_f80 a, i8087_f80 b, unsigned p, unsigned rc)
{
    enum sf_kind ka = sf_kind(a), kb = sf_kind(b);
    bool sign = sf_sign(a) != sf_sign(b);
    unpacked ua, ub;
    wide prod;
    if (ka == SF_NAN || kb == SF_NAN)
        return nan_result(a, b);
    if ((ka == SF_ZERO && kb == SF_INFINITY) || (kb == SF_ZERO && ka == SF_INFINITY))
        return result(sf_indefinite(), I8087_IE);
    if (ka == SF_INFINITY || kb == SF_INFINITY)
        return result(sf_infinity(sign), 0);
    if (ka == SF_ZERO || kb == SF_ZERO)
        return result(sf_zero(sign), 0);
    ua = unpack(a);
    ub = unpack(b);
    prod = wmul(ua.sig, ub.sig);
    return finish(sign, ua.exp + ub.exp, wjam(prod, 60), p, rc);
}

sf_result sf_divide(i8087_f80 a, i8087_f80 b, unsigned p, unsigned rc)
{
    enum sf_kind ka = sf_kind(a), kb = sf_kind(b);
    bool sign = sf_sign(a) != sf_sign(b);
    unpacked ua, ub;
    wide rem, div, q = w64(0);
    int32_t e;
    unsigned i;
    if (ka == SF_NAN || kb == SF_NAN)
        return nan_result(a, b);
    if ((ka == SF_ZERO && kb == SF_ZERO) || (ka == SF_INFINITY && kb == SF_INFINITY))
        return result(sf_indefinite(), I8087_IE);
    if (ka == SF_INFINITY)
        return result(sf_infinity(sign), 0);
    if (kb == SF_INFINITY)
        return result(sf_zero(sign), 0);
    if (kb == SF_ZERO)
        return result(sf_infinity(sign), I8087_ZE);
    if (ka == SF_ZERO)
        return result(sf_zero(sign), 0);
    ua = unpack(a);
    ub = unpack(b);
    e = ua.exp - ub.exp;
    rem = w64(ua.sig);
    div = w64(ub.sig);
    if (wcmp(rem, div) < 0) {
        rem = wleft(rem, 1);
        --e;
    }
    for (i = 0; i < 67; ++i) {
        q = wleft(q, 1);
        if (wcmp(rem, div) >= 0) {
            rem = wsub(rem, div);
            q.lo |= 1;
        }
        if (i != 66)
            rem = wleft(rem, 1);
    }
    if (wnz(rem))
        q.lo |= 1;
    return finish(sign, e, q, p, rc);
}

sf_result sf_sqrt(i8087_f80 x, unsigned p, unsigned rc)
{
    enum sf_kind k = sf_kind(x);
    unpacked u;
    wide root = w64(0), rem = w64(0), trial;
    unsigned shift;
    int i;
    if (k == SF_NAN)
        return nan_result(x, x);
    if (k == SF_ZERO)
        return result(x, 0);
    if (sf_sign(x))
        return result(sf_indefinite(), I8087_IE);
    if (k == SF_INFINITY)
        return result(x, 0);
    u = unpack(x);
    shift = 69u + (unsigned)(u.exp % 2 != 0);
    if (u.exp % 2 != 0)
        --u.exp;
    for (i = 66; i >= 0; --i) {
        unsigned j, pair = 0;
        for (j = 0; j < 2; ++j) {
            int bit = 2 * i + (int)(1 - j) - (int)shift;
            pair = (pair << 1) |
                   ((bit >= 0 && bit < 64) ? (unsigned)((u.sig >> (unsigned)bit) & 1) : 0u);
        }
        rem = wleft(rem, 2);
        rem.lo |= pair;
        trial = wleft(root, 2);
        trial.lo |= 1;
        root = wleft(root, 1);
        if (wcmp(rem, trial) >= 0) {
            rem = wsub(rem, trial);
            root.lo |= 1;
        }
    }
    if (wnz(rem))
        root.lo |= 1;
    return finish(false, u.exp / 2, root, p, rc);
}

sf_result sf_scale(i8087_f80 x, int32_t delta, unsigned rc)
{
    unpacked u;
    if (sf_kind(x) != SF_FINITE)
        return result(x, 0);
    u = unpack(x);
    if (delta > 65536)
        delta = 65536;
    if (delta < -65536)
        delta = -65536;
    return finish(u.sign, u.exp + delta, wleft(w64(u.sig), 3), 64, rc);
}

int sf_compare(i8087_f80 a, i8087_f80 b)
{
    enum sf_kind ka = sf_kind(a), kb = sf_kind(b);
    unpacked ua, ub;
    int m;
    if (ka == SF_NAN || kb == SF_NAN)
        return 2;
    if (ka == SF_ZERO && kb == SF_ZERO)
        return 0;
    if (sf_sign(a) != sf_sign(b))
        return sf_sign(a) ? -1 : 1;
    if (ka == SF_INFINITY || kb == SF_INFINITY)
        m = ka == kb ? 0 : (ka == SF_INFINITY ? 1 : -1);
    else if (ka == SF_ZERO || kb == SF_ZERO)
        m = ka == kb ? 0 : (ka == SF_ZERO ? -1 : 1);
    else {
        ua = unpack(a);
        ub = unpack(b);
        m = ua.exp != ub.exp ? (ua.exp > ub.exp ? 1 : -1)
                             : (ua.sig == ub.sig ? 0 : (ua.sig > ub.sig ? 1 : -1));
    }
    return sf_sign(a) ? -m : m;
}

static uint64_t integer_magnitude(unpacked u, unsigned rc, bool *lost, bool *up, bool *overflow)
{
    uint64_t v, rem = 0, half = 0;
    unsigned shift;
    *lost = false;
    *up = false;
    *overflow = false;
    if (u.exp > 63) {
        *overflow = true;
        return 0;
    }
    if (u.exp == 63)
        return u.sig;
    if (u.exp < 0) {
        v = 0;
        *lost = u.sig != 0;
        if (rc == 0)
            *up = u.exp == -1 && u.sig > JBIT;
        else if (rc == 1)
            *up = u.sign && *lost;
        else if (rc == 2)
            *up = !u.sign && *lost;
    } else {
        shift = (unsigned)(63 - u.exp);
        v = u.sig >> shift;
        rem = u.sig & ((UINT64_C(1) << shift) - 1);
        half = UINT64_C(1) << (shift - 1);
        *lost = rem != 0;
        if (rc == 0)
            *up = rem > half || (rem == half && (v & 1));
        else if (rc == 1)
            *up = u.sign && *lost;
        else if (rc == 2)
            *up = !u.sign && *lost;
    }
    if (*up) {
        ++v;
        if (!v)
            *overflow = true;
    }
    return v;
}

uint64_t sf_to_integer(i8087_f80 x, unsigned width, unsigned rc, uint16_t *flags, bool *up)
{
    uint64_t limit = UINT64_C(1) << (width - 1), v;
    bool lost, overflow;
    unpacked u = unpack(x);
    *up = false;
    if (sf_kind(x) == SF_ZERO)
        return 0;
    if (sf_kind(x) != SF_FINITE) {
        *flags |= I8087_IE;
        return limit;
    }
    v = integer_magnitude(u, rc, &lost, up, &overflow);
    if (overflow || v > limit || (!u.sign && v == limit)) {
        *flags |= I8087_IE;
        *up = false;
        return limit;
    }
    if (lost)
        *flags |= I8087_PE;
    return u.sign ? (UINT64_C(0) - v) : v;
}

sf_result sf_round_integer(i8087_f80 x, unsigned rc)
{
    unpacked u = unpack(x);
    uint64_t v;
    bool lost, up, overflow;
    sf_result r;
    if (sf_kind(x) == SF_NAN)
        return nan_result(x, x);
    if (sf_kind(x) == SF_ZERO)
        return result(sf_zero(u.sign), 0);
    if (sf_kind(x) != SF_FINITE)
        return result(x, 0);
    if (u.exp >= 63)
        return result(sf_normalize(x), 0);
    v = integer_magnitude(u, rc, &lost, &up, &overflow);
    (void)overflow;
    r = result(sf_from_u64(v, u.sign), lost ? I8087_PE : 0);
    r.rounded_up = up;
    return r;
}

i8087_f80 sf_remainder(i8087_f80 a, i8087_f80 b, uint8_t *quotient, bool *partial, unsigned *qwidth)
{
    unpacked ua = unpack(a), ub = unpack(b);
    wide rem = w64(ua.sig), div = w64(ub.sig);
    int32_t d = ua.exp - ub.exp, remaining = 0;
    unsigned steps, i;
    uint8_t q = 0;
    *quotient = 0;
    *partial = false;
    *qwidth = 1;
    if (sf_kind(a) == SF_ZERO)
        return sf_zero(ua.sign);
    if (sf_kind(b) == SF_INFINITY)
        return sf_normalize(a);
    if (d < 0)
        return sf_normalize(a);
    *partial = d >= 64;
    if (*partial) {
        remaining = d - 31;
        d = 31;
    }
    steps = (unsigned)d + 1;
    for (i = 0; i < steps; ++i) {
        q = (uint8_t)((q << 1) & 7);
        if (wcmp(rem, div) >= 0) {
            rem = wsub(rem, div);
            q |= 1;
        }
        if (i + 1 < steps)
            rem = wleft(rem, 1);
    }
    *quotient = q;
    /* A leading zero quotient trial is not a generated quotient bit.
       Even a zero quotient shifts the old 8087 CC register once. */
    *qwidth = steps - (ua.sig < ub.sig ? 1u : 0u);
    if (!*qwidth)
        *qwidth = 1;
    return finish(ua.sign, ub.exp + remaining, wleft(rem, 3), 64, 0).value;
}
