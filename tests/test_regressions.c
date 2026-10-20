/* SPDX-License-Identifier: MIT
 * Original-8087 instruction regression tests. These expected values are
 * document/algorithm-derived, NOT captures from original hardware. */
#include "i8087.h"
#include "soft80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JBIT UINT64_C(0x8000000000000000)
#define CC (I8087_C0 | I8087_C1 | I8087_C2 | I8087_C3)
static unsigned long checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint8_t memory[256];
static bool fail_write;
static unsigned writes;

static bool rd(void *ctx, uint32_t a, uint8_t *p, size_t n) {
    (void)ctx;
    if (a > sizeof(memory) || n > sizeof(memory) - a)
        return false;
    memcpy(p, memory + a, n);
    return true;
}

static bool wr(void *ctx, uint32_t a, const uint8_t *p, size_t n) {
    (void)ctx;
    if (fail_write || a > sizeof(memory) || n > sizeof(memory) - a)
        return false;
    memcpy(memory + a, p, n);
    ++writes;
    return true;
}

static const i8087_bus bus = {NULL, rd, wr};

static bool eq(i8087_f80 a, i8087_f80 b) {
    return a.significand == b.significand && a.sign_exponent == b.sign_exponent;
}

static void slot(i8087 *f, unsigned i, i8087_f80 x, unsigned tag) {
    unsigned p = (i8087_top(f) + i) & 7;
    f->registers[p] = x;
    f->tags = (uint16_t)((f->tags & ~(3u << (2 * p))) | (tag << (2 * p)));
}

static void number(i8087 *f, unsigned i, i8087_f80 x) {
    slot(f, i, x, sf_tag(x));
}

static i8087_f80 st(i8087 *f, unsigned i) {
    i8087_f80 x;
    CHECK(i8087_get_st(f, i, &x));
    return x;
}

static i8087_result op(i8087 *f, uint8_t esc, uint8_t m) {
    return i8087_execute(f, &bus, esc, m, 0x12345, 0);
}

static void ok(i8087 *f, uint8_t esc, uint8_t m) {
    CHECK(op(f, esc, m) == I8087_OK);
    CHECK(f->last_gap == 0);
}

static i8087_f80 integer(int n) {
    return sf_from_u64((uint64_t)(n < 0 ? -n : n), n < 0);
}

static uint16_t cc_from_q(unsigned q) {
    return (uint16_t)(((q & 4) ? I8087_C0 : 0) | ((q & 2) ? I8087_C3 : 0) |
                      ((q & 1) ? I8087_C1 : 0));
}

static bool same_registers(const i8087 *a, const i8087 *b) {
    unsigned i;
    if (a->tags != b->tags || i8087_top(a) != i8087_top(b))
        return false;
    for (i = 0; i < 8; ++i)
        if (!eq(a->registers[i], b->registers[i]))
            return false;
    return true;
}

static void extract_fields(void) {
    const uint16_t exponents[] = {0, 1, 17, 0x3ffe, 0x3fff, 0x4000, 0x7ffe};
    const uint64_t fractions[] = {0, 1, UINT64_C(0x123456789abcdef), JBIT, JBIT + 1, UINT64_MAX};
    unsigned e, v, sign, pc, rc;
    for (e = 0; e < sizeof(exponents) / sizeof(exponents[0]); ++e)
        for (v = 0; v < sizeof(fractions) / sizeof(fractions[0]); ++v)
            for (sign = 0; sign < 2; ++sign)
                for (pc = 0; pc < 4; ++pc)
                    for (rc = 0; rc < 4; ++rc) {
                        i8087 f;
                        i8087_f80 input = {fractions[v], (uint16_t)(exponents[e] | (sign << 15))};
                        i8087_f80 expected_exp = integer((int)exponents[e] - 16383),
                                  expected_sig = input;
                        i8087_init(&f);
                        number(&f, 0, input);
                        f.control = (uint16_t)(0x00bf | (pc << 8) | (rc << 10));
                        if (!fractions[v] && !exponents[e])
                            expected_exp = expected_sig = input;
                        else
                            expected_sig.sign_exponent = (uint16_t)(0x3fff | (sign << 15));
                        ok(&f, 0xd9, 0xf4);
                        CHECK(eq(st(&f, 0), expected_sig));
                        CHECK(eq(st(&f, 1), expected_exp));
                        CHECK(!(f.status & 0x3f));
                        CHECK(i8087_top(&f) == 7);
                    }
}

static void remainder_flags(void) {
    unsigned a, b, sign, old, rc, pc;
    for (a = 0; a <= 257; ++a)
        for (b = 1; b <= 11; ++b)
            for (sign = 0; sign < 4; ++sign)
                for (old = 0; old < 8; ++old) {
                    i8087 f;
                    unsigned quotient = a / b, width = quotient < 2 ? 1 : (quotient < 4 ? 2 : 3);
                    unsigned expected = width < 3 ? ((old << width) | quotient) & 7 : quotient & 7;
                    i8087_init(&f);
                    number(&f, 0, sf_from_u64(a, (sign & 1) != 0));
                    number(&f, 1, sf_from_u64(b, (sign & 2) != 0));
                    f.status |= (uint16_t)(cc_from_q(old) | I8087_C2);
                    ok(&f, 0xd9, 0xf8);
                    CHECK(eq(st(&f, 0), sf_from_u64(a % b, (sign & 1) != 0)));
                    CHECK((f.status & CC) == cc_from_q(expected));
                    CHECK(!(f.status & 0x3f));
                }
    for (pc = 0; pc < 4; ++pc)
        for (rc = 0; rc < 4; ++rc) {
            i8087 f;
            i8087_init(&f);
            f.control = (uint16_t)(0x00bf | (pc << 8) | (rc << 10));
            number(&f, 0, (i8087_f80){JBIT + 123, 0x403e});
            number(&f, 1, integer(1));
            ok(&f, 0xd9, 0xf8);
            CHECK(eq(st(&f, 0), integer(0)));
            CHECK(!(f.status & I8087_PE));
            CHECK((f.status & CC) == cc_from_q(3));
        }
    {
        i8087 f;
        i8087_init(&f);
        number(&f, 0, integer(7));
        number(&f, 1, integer(0));
        f.status |= I8087_C2;
        ok(&f, 0xd9, 0xf8);
        CHECK(f.status & I8087_IE);
        CHECK(!(f.status & I8087_C2));
    }
    {
        i8087 f;
        i8087_init(&f);
        number(&f, 0, (i8087_f80){JBIT, 0x4100});
        number(&f, 1, integer(3));
        CHECK(op(&f, 0xd9, 0xf8) == I8087_UNVERIFIED);
        CHECK(f.last_gap & I8087_GAP_PARTIAL_REMAINDER);
    }
    {
        i8087 f;
        i8087_init(&f);
        number(&f, 0, (i8087_f80){UINT64_C(0x1000000000000000), 0x4004});
        number(&f, 1, integer(3));
        ok(&f, 0xd9, 0xf8);
        CHECK(eq(st(&f, 0), integer(1)));
    }
}

static void scale_early_exit_and_fields(void) {
    unsigned sign, which, tag;
    const i8087_f80 special = {UINT64_C(0xc123456789abcdef), 0x7fff};
    for (sign = 0; sign < 2; ++sign)
        for (which = 0; which < 2; ++which)
            for (tag = 0; tag < 4; ++tag) {
                i8087 f, before;
                i8087_init(&f);
                f.control &= (uint16_t)~0x3f;
                number(&f, which, (i8087_f80){0, (uint16_t)(sign << 15)});
                slot(&f, which ^ 1, special, tag);
                before = f;
                ok(&f, 0xd9, 0xfd);
                CHECK(same_registers(&f, &before));
                CHECK(!(f.status & 0x3f));
            }
    for (sign = 0; sign < 2; ++sign) {
        int delta;
        for (delta = -31; delta <= 31; ++delta) {
            i8087 f;
            i8087_f80 raw = {UINT64_C(0x123456789abcdef), (uint16_t)(0x4000 | (sign << 15))};
            i8087_f80 expected = raw;
            i8087_init(&f);
            number(&f, 0, raw);
            number(&f, 1, integer(delta));
            expected.sign_exponent = (uint16_t)((unsigned)(0x4000 + delta) | (sign << 15));
            ok(&f, 0xd9, 0xfd);
            CHECK(eq(st(&f, 0), expected));
            CHECK(i8087_tag(&f, 0) == I8087_TAG_VALID);
        }
    }
    {
        i8087 f, before;
        i8087_init(&f);
        number(&f, 0, integer(5));
        number(&f, 1, (i8087_f80){JBIT, 0x3ffe});
        before = f;
        CHECK(op(&f, 0xd9, 0xfd) == I8087_UNVERIFIED);
        CHECK(same_registers(&f, &before));
        CHECK(f.last_gap & I8087_GAP_UNDEFINED_OPERAND);
    }
    {
        i8087 f;
        i8087_init(&f);
        number(&f, 0, (i8087_f80){1, 0});
        number(&f, 1, integer(3));
        ok(&f, 0xd9, 0xfd);
        CHECK(eq(st(&f, 0), (i8087_f80){1, 4}));
        CHECK(f.status & I8087_DE);
    }
}

static void tangent_tiny(void) {
    const uint16_t exponents[] = {1, 17, 0x3f00, 0x3fbe, 0x3fbf};
    unsigned e, pc, rc, sign;
    for (e = 0; e < sizeof(exponents) / sizeof(exponents[0]); ++e)
        for (pc = 0; pc < 4; ++pc)
            for (rc = 0; rc < 4; ++rc) {
                i8087 f;
                i8087_f80 a = {JBIT + 12345, exponents[e]};
                i8087_init(&f);
                f.control = (uint16_t)(0x00bf | (pc << 8) | (rc << 10));
                number(&f, 0, a);
                ok(&f, 0xd9, 0xf2);
                CHECK(eq(st(&f, 0), integer(1)));
                CHECK(eq(st(&f, 1), a));
                CHECK((f.status & 0x3f) == I8087_PE);
                CHECK(!f.used_approximations);
            }
    for (sign = 0; sign < 2; ++sign) {
        i8087 f;
        i8087_f80 z = {0, (uint16_t)(sign << 15)};
        i8087_init(&f);
        number(&f, 0, z);
        ok(&f, 0xd9, 0xf2);
        CHECK(eq(st(&f, 0), integer(1)));
        CHECK(eq(st(&f, 1), z));
        CHECK(!(f.status & 0x3f));
    }
    {
        i8087 f;
        i8087_init(&f);
        number(&f, 0, (i8087_f80){JBIT, 0x3fc0});
        CHECK(op(&f, 0xd9, 0xf2) == I8087_UNVERIFIED);
        CHECK(f.last_gap & I8087_GAP_TRANSCENDENTAL);
    }
    {
        i8087 f;
        i8087_init(&f);
        number(&f, 0, (i8087_f80){JBIT, 0x3fbf});
        f.control &= (uint16_t)~I8087_PE;
        CHECK(op(&f, 0xd9, 0xf2) == I8087_PENDING_INTERRUPT);
        CHECK(i8087_top(&f) == 7);
        CHECK(eq(st(&f, 0), integer(1)));
        CHECK((f.status & (I8087_PE | I8087_IR)) == (I8087_PE | I8087_IR));
    }
}

static void pseudozeros_and_tags(void) {
    i8087 f;
    unsigned tag, sign;
    for (sign = 0; sign < 2; ++sign) {
        i8087_f80 raw = {0, (uint16_t)(0x4000 | (sign << 15))};
        i8087_init(&f);
        number(&f, 0, raw);
        CHECK(i8087_tag(&f, 0) == I8087_TAG_VALID);
        ok(&f, 0xd9, 0xe5);
        CHECK((f.status & CC) == (sign ? I8087_C1 : 0));
        number(&f, 1, raw);
        number(&f, 0, integer(1));
        ok(&f, 0xd8, 0xf1);
        CHECK(f.status & I8087_IE);
        CHECK(!(f.status & I8087_ZE));
        i8087_init(&f);
        number(&f, 0, raw);
        ok(&f, 0xd9, 0xfc);
        CHECK(eq(st(&f, 0), sf_zero(sign != 0)));
    }
    /* Register transfers copy tags rather than reclassifying the data bytes.
       These deliberately mismatched tag images exercise FRSTOR-style input. */
    for (tag = 0; tag < 3; ++tag) {
        i8087_f80 raw = {UINT64_C(0x123456789abcdef0), 0x4321};
        i8087_init(&f);
        slot(&f, 0, raw, tag);
        number(&f, 1, integer(3));
        ok(&f, 0xd9, 0xc0);
        CHECK(i8087_tag(&f, 0) == tag);
        CHECK(eq(st(&f, 0), raw));
        ok(&f, 0xd9, 0xca);
        CHECK(i8087_tag(&f, 2) == tag);
        CHECK(eq(st(&f, 2), raw));
        ok(&f, 0xd9, 0xca);
        ok(&f, 0xdd, 0xd3);
        CHECK(i8087_tag(&f, 3) == tag);
        CHECK(eq(st(&f, 3), raw));
        ok(&f, 0xd9, 0xe0);
        CHECK(i8087_tag(&f, 0) == tag);
        ok(&f, 0xd9, 0xe1);
        CHECK(i8087_tag(&f, 0) == tag);
    }
}

static void exceptions_and_interrupt_latch(void) {
    i8087 f, before;
    unsigned count;
    /* A precision trap happens AFTER rounding and AFTER any store/pop. */
    i8087_init(&f);
    number(&f, 0, integer(1));
    number(&f, 1, integer(3));
    f.control &= (uint16_t)~I8087_PE;
    CHECK(op(&f, 0xd8, 0xf1) == I8087_PENDING_INTERRUPT);
    CHECK(!f.last_gap);
    CHECK(eq(st(&f, 0), (i8087_f80){UINT64_C(0xaaaaaaaaaaaaaaab), 0x3ffd}));
    CHECK((f.status & (I8087_PE | I8087_IR | I8087_BUSY)) == (I8087_PE | I8087_IR | I8087_BUSY));
    CHECK(!i8087_interrupt_asserted(&f));
    /* Without WAIT, another ESC may run with a pending masked-global IRQ. */
    ok(&f, 0xd9, 0xe8);
    CHECK(eq(st(&f, 0), integer(1)));
    CHECK(f.status & I8087_IR);
    CHECK(i8087_wait(&f) == I8087_PENDING_INTERRUPT);
    CHECK(i8087_interrupt_asserted(&f));
    before = f;
    CHECK(op(&f, 0xd9, 0xe8) == I8087_PENDING_INTERRUPT);
    CHECK(same_registers(&f, &before));
    ok(&f, 0xdb, 0xe2);
    CHECK(!(f.status & (I8087_IR | I8087_BUSY | 0x3f)));
    i8087_init(&f);
    number(&f, 0, (i8087_f80){UINT64_C(0xc000000000000000), 0x3fff});
    f.control &= (uint16_t)~I8087_PE;
    memory[0] = 99;
    count = writes;
    CHECK(op(&f, 0xdf, 0x1e) == I8087_PENDING_INTERRUPT);
    CHECK(writes == count + 1);
    CHECK(memory[0] == 2 && memory[1] == 0);
    CHECK(i8087_top(&f) == 1);
    CHECK(f.tags == 0xffff);
    i8087_init(&f);
    number(&f, 0, (i8087_f80){JBIT + 1, 0x3fff});
    f.control &= (uint16_t)~I8087_PE;
    CHECK(op(&f, 0xd9, 0x1e) == I8087_PENDING_INTERRUPT);
    CHECK(memory[3] == 0x3f && memory[2] == 0x80);
    CHECK(i8087_top(&f) == 1);
    CHECK(!f.last_gap);
    /* Callback failure must also roll back the newly latched after exception. */
    i8087_init(&f);
    number(&f, 0, (i8087_f80){JBIT + 1, 0x3fff});
    f.control &= (uint16_t)~I8087_PE;
    before = f;
    fail_write = true;
    CHECK(op(&f, 0xd9, 0x1e) == I8087_MEMORY_FAULT);
    fail_write = false;
    CHECK(same_registers(&f, &before));
    CHECK(f.status == before.status);
    /* Unmasking an old sticky exception does NOT synthesize a new event. */
    i8087_init(&f);
    f.status = I8087_PE;
    memory[0] = 0x9f;
    memory[1] = 3;
    ok(&f, 0xd9, 0x2e);
    CHECK(!(f.status & (I8087_IR | I8087_BUSY)));
    CHECK(f.status & I8087_PE);
    number(&f, 0, integer(1));
    ok(&f, 0xd8, 0xc0);
    CHECK(!(f.status & I8087_IR));
    /* Masking a latched exception does not erase it either. */
    f.status |= I8087_IR | I8087_BUSY;
    memory[0] = 0xbf;
    memory[1] = 3;
    ok(&f, 0xd9, 0x2e);
    CHECK((f.status & (I8087_IR | I8087_BUSY)) == (I8087_IR | I8087_BUSY));
    ok(&f, 0xd9, 0x36);
    CHECK(f.status & I8087_IR);
    ok(&f, 0xdb, 0xe2);
    CHECK(!(f.status & I8087_IR));
    /* Before exceptions leave both operands and the stack unchanged. */
    i8087_init(&f);
    number(&f, 0, integer(1));
    number(&f, 1, integer(0));
    f.control &= (uint16_t)~I8087_ZE;
    before = f;
    CHECK(op(&f, 0xd8, 0xf1) == I8087_PENDING_INTERRUPT);
    CHECK(same_registers(&f, &before));
    CHECK(!f.last_gap);
    i8087_init(&f);
    number(&f, 0, integer(0));
    number(&f, 1, integer(0));
    f.control &= (uint16_t)~I8087_IE;
    before = f;
    CHECK(op(&f, 0xde, 0xf9) == I8087_PENDING_INTERRUPT);
    CHECK(same_registers(&f, &before));
    CHECK(!f.last_gap);
    i8087_init(&f);
    f.control &= (uint16_t)~I8087_DE;
    memset(memory, 0, 4);
    memory[0] = 1;
    before = f;
    CHECK(op(&f, 0xd9, 0x06) == I8087_PENDING_INTERRUPT);
    CHECK(same_registers(&f, &before));
    CHECK((f.status & 0x3f) == I8087_DE);
}

static void range_destinations_and_nan_signs(void) {
    i8087 f, before;
    unsigned sign, rc, operation;
    for (sign = 0; sign < 2; ++sign)
        for (rc = 0; rc < 4; ++rc) {
            i8087_f80 tiny = {JBIT, (uint16_t)(1 | (sign << 15))};
            i8087_f80 huge = {JBIT, (uint16_t)(0x7ffe | (sign << 15))};
            i8087_init(&f);
            f.control = (uint16_t)((0x3bf | (rc << 10)) & ~(unsigned)I8087_UE);
            number(&f, 0, tiny);
            number(&f, 1, (i8087_f80){JBIT, 0x3ffe});
            CHECK(op(&f, 0xd8, 0xc9) == I8087_PENDING_INTERRUPT);
            CHECK(!f.last_gap);
            CHECK(eq(st(&f, 0), (i8087_f80){JBIT, (uint16_t)(0x6000 | (sign << 15))}));
            CHECK((f.status & 0x3f) == I8087_UE);
            i8087_init(&f);
            f.control = (uint16_t)((0x3bf | (rc << 10)) & ~(unsigned)I8087_OE);
            number(&f, 0, huge);
            number(&f, 1, integer(2));
            CHECK(op(&f, 0xd8, 0xc9) == I8087_PENDING_INTERRUPT);
            CHECK(!f.last_gap);
            CHECK(eq(st(&f, 0), (i8087_f80){JBIT, (uint16_t)(0x1fff | (sign << 15))}));
            CHECK((f.status & 0x3f) == I8087_OE);
            /* Masked exact underflow preserves a subnormal and reports UE alone. */
            i8087_init(&f);
            f.control = (uint16_t)(0x3bf | (rc << 10));
            number(&f, 0, tiny);
            number(&f, 1, (i8087_f80){JBIT, 0x3ffe});
            ok(&f, 0xd8, 0xc9);
            CHECK(eq(st(&f, 0), (i8087_f80){JBIT >> 1, (uint16_t)(sign << 15)}));
            CHECK((f.status & 0x3f) == I8087_UE);
            /* Exact minimum float subnormal: store reports UE without PE. */
            i8087_init(&f);
            number(&f, 0, (i8087_f80){JBIT, (uint16_t)((16383 - 149) | (sign << 15))});
            ok(&f, 0xd9, 0x1e);
            CHECK(memory[0] == 1 && memory[1] == 0 && memory[2] == 0 && memory[3] == sign * 128);
            CHECK((f.status & 0x3f) == I8087_UE);
            CHECK(i8087_top(&f) == 1);
            /* Unmasked range conversion: NO write, NO pop, NO precision stage. */
            i8087_init(&f);
            f.control &= (uint16_t)~(I8087_UE | I8087_PE);
            number(&f, 0, (i8087_f80){JBIT + 1, (uint16_t)((16383 - 150) | (sign << 15))});
            before = f;
            memset(memory, 0x5a, 8);
            CHECK(op(&f, 0xd9, 0x1e) == I8087_PENDING_INTERRUPT);
            CHECK(same_registers(&f, &before));
            CHECK(memory[0] == 0x5a && memory[3] == 0x5a);
            CHECK((f.status & 0x3f) == I8087_UE);
            CHECK(!f.last_gap);
        }
    /* A NaN selected by subtraction must not acquire an artificial minus sign. */
    for (operation = 0; operation < 8; ++operation) {
        const i8087_f80 nan = {UINT64_C(0xc123456789abcdef), 0xffff};
        if (operation == 2 || operation == 3)
            continue;
        i8087_init(&f);
        number(&f, 0, integer(5));
        number(&f, 1, nan);
        ok(&f, 0xd8, (uint8_t)(0xc1 | (operation << 3)));
        CHECK(eq(st(&f, 0), nan));
        CHECK((f.status & 0x3f) == I8087_IE);
    }
    /* DE must preempt a would-be unnormal arithmetic/range computation. */
    i8087_init(&f);
    f.control &= (uint16_t)~I8087_DE;
    number(&f, 0, integer(1));
    number(&f, 1, (i8087_f80){1, 0});
    before = f;
    CHECK(op(&f, 0xd8, 0xc1) == I8087_PENDING_INTERRUPT);
    CHECK(!f.last_gap);
    CHECK(same_registers(&f, &before));
    CHECK((f.status & 0x3f) == I8087_DE);
    i8087_init(&f);
    number(&f, 0, (i8087_f80){JBIT >> 1, 0x4000});
    number(&f, 1, integer(1));
    ok(&f, 0xd8, 0xd1);
    CHECK((f.status & 0x3f) == I8087_DE);
    CHECK(f.status & I8087_C3);
    i8087_init(&f);
    number(&f, 0, (i8087_f80){JBIT >> 1, 0x4000});
    number(&f, 1, integer(1));
    f.control &= (uint16_t)~I8087_DE;
    f.status |= I8087_C0 | I8087_C1;
    before = f;
    CHECK(op(&f, 0xd8, 0xd9) == I8087_PENDING_INTERRUPT);
    CHECK(!f.last_gap);
    CHECK(same_registers(&f, &before));
    CHECK((f.status & CC) == (before.status & CC));
}

static void round_integer_raw_encodings(void) {
    /* Intel S-70 Table S-25 requires normalization before rounding an unnormal.
       Expected encodings below are literals, not results from soft80 helpers. */
    static const i8087_f80 results[] = {{0, 0},
                                        {JBIT, 0x3fff},
                                        {JBIT, 0x4000},
                                        {UINT64_C(0xc000000000000000), 0x4000},
                                        {UINT64_MAX, 0x403e},
                                        {JBIT, 0x403f},
                                        {JBIT + 2, 0x403f}};

    static const struct {
        i8087_f80 input;
        unsigned positive[4], negative[4];
        bool fractional, denormal;
    } cases[] = {
        {{1, 0x403e}, {1, 1, 1, 1}, {1, 1, 1, 1}, false, false},
        {{UINT64_C(0x4000000000000000), 0x4040}, {5, 5, 5, 5}, {5, 5, 5, 5}, false, false},
        {{0, 0x4040}, {0, 0, 0, 0}, {0, 0, 0, 0}, false, false},
        {{1, 0x403d}, {0, 0, 1, 0}, {0, 1, 0, 0}, true, false},
        {{3, 0x403d}, {2, 1, 2, 1}, {2, 2, 1, 1}, true, false},
        {{5, 0x403d}, {2, 2, 3, 2}, {2, 3, 2, 2}, true, false},
        {{1, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}, true, false},
        {{1, 0}, {0, 0, 1, 0}, {0, 1, 0, 0}, true, true},
        {{JBIT, 0x3ffe}, {0, 0, 1, 0}, {0, 1, 0, 0}, true, false},
        {{0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, false, false},
        {{UINT64_MAX, 0x403e}, {4, 4, 4, 4}, {4, 4, 4, 4}, false, false},
        {{UINT64_C(0x4000000000000001), 0x4040}, {6, 6, 6, 6}, {6, 6, 6, 6}, false, false}};

    static const unsigned tops[] = {0, 3, 7};
    unsigned c, sign, pc, rc, masks, top_index, p;
    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
        for (sign = 0; sign < 2; ++sign)
            for (pc = 0; pc < 4; ++pc)
                for (rc = 0; rc < 4; ++rc)
                    for (masks = 0; masks < 4; ++masks)
                        for (top_index = 0; top_index < 3; ++top_index) {
                            i8087 f, before;
                            i8087_f80 input = cases[c].input,
                                      expected = results[sign ? cases[c].negative[rc]
                                                              : cases[c].positive[rc]];
                            uint16_t disabled = (uint16_t)(((masks & 1) ? I8087_DE : 0) |
                                                           ((masks & 2) ? I8087_PE : 0));
                            uint16_t flags = (uint16_t)((cases[c].denormal ? I8087_DE : 0) |
                                                        (cases[c].fractional ? I8087_PE : 0));
                            bool abort = cases[c].denormal && (masks & 1);
                            bool pending;
                            unsigned count = writes;
                            input.sign_exponent |= (uint16_t)(sign << 15);
                            expected.sign_exponent |= (uint16_t)(sign << 15);
                            if (abort) {
                                expected = input;
                                flags = I8087_DE;
                            }
                            pending = (flags & disabled) != 0;
                            i8087_init(&f);
                            f.control = (uint16_t)((0x00bfu | (pc << 8) | (rc << 10)) & ~disabled);
                            f.status = (uint16_t)((tops[top_index] << 11) | CC | I8087_IE);
                            slot(&f, 0, input,
                                 cases[c].denormal
                                     ? I8087_TAG_SPECIAL
                                     : (!input.significand && !(input.sign_exponent & 0x7fff)
                                            ? I8087_TAG_ZERO
                                            : I8087_TAG_VALID));
                            before = f;
                            CHECK(op(&f, 0xd9, 0xfc) ==
                                  (pending ? I8087_PENDING_INTERRUPT : I8087_OK));
                            CHECK(eq(st(&f, 0), expected));
                            CHECK(f.last_gap == 0);
                            CHECK(f.used_approximations == 0);
                            CHECK((f.status & 0x3f) == (I8087_IE | flags));
                            CHECK((f.status & (I8087_IR | I8087_BUSY)) ==
                                  (pending ? (I8087_IR | I8087_BUSY) : 0));
                            CHECK(i8087_top(&f) == tops[top_index]);
                            CHECK(f.control == before.control);
                            CHECK(i8087_tag(&f, 0) ==
                                  (unsigned)(abort ? I8087_TAG_SPECIAL
                                                   : (expected.significand ? I8087_TAG_VALID
                                                                           : I8087_TAG_ZERO)));
                            CHECK(writes == count);
                            for (p = 0; p < 8; ++p)
                                if (p != tops[top_index]) {
                                    CHECK(eq(f.registers[p], before.registers[p]));
                                    CHECK(((f.tags >> (2 * p)) & 3) == I8087_TAG_EMPTY);
                                }
                        }
}

static void masked_directed_overflow(void) {
    static const unsigned pcs[] = {0, 2, 3};
    static const uint8_t escapes[] = {0xd8, 0xd8, 0xd8, 0xd8, 0xdc, 0xde, 0xd8};
    static const uint8_t encodings[] = {0xc1, 0xe1, 0xc9, 0xf1, 0xc1, 0xc1, 0x0e};
    unsigned pc, sign, rc, trap, sticky, form, width, popping, i;
    for (pc = 0; pc < 3; ++pc)
        for (sign = 0; sign < 2; ++sign)
            for (rc = 0; rc < 3; ++rc)
                for (trap = 0; trap < 2; ++trap)
                    for (sticky = 0; sticky < 2; ++sticky)
                        for (form = 0; form < 7; ++form) {
                            i8087 f;
                            i8087_f80 a = {UINT64_MAX, (uint16_t)(0x7ffe | (sign << 15))}, b = a,
                                      expected;
                            bool finite = (sign && rc == 2) || (!sign && rc == 1);
                            unsigned result_slot = form == 4 ? 1u : 0u, count = writes;
                            uint16_t expected_flags =
                                (uint16_t)(I8087_IE | I8087_PE |
                                           ((rc == 0 || sticky) ? I8087_OE : 0));
                            if (form == 1)
                                b.sign_exponent ^= 0x8000;
                            else if (form == 2 || form == 6)
                                b = (i8087_f80){JBIT, 0x4000};
                            else if (form == 3)
                                b = (i8087_f80){JBIT, 0x3ffe};
                            expected =
                                (i8087_f80){finite ? UINT64_MAX : JBIT,
                                            (uint16_t)((finite ? 0x7ffe : 0x7fff) | (sign << 15))};
                            i8087_init(&f);
                            number(&f, 0, a);
                            number(&f, 1, b);
                            f.control = (uint16_t)((0x00bfu | (pcs[pc] << 8) | (rc << 10)) &
                                                   ~(trap ? (unsigned)I8087_PE : 0u));
                            f.status = (uint16_t)(I8087_IE | (sticky ? I8087_OE : 0));
                            memory[0] = 0;
                            memory[1] = 0;
                            memory[2] = 0;
                            memory[3] = 0x40; /* binary32 +2 */
                            CHECK(op(&f, escapes[form], encodings[form]) ==
                                  (trap ? I8087_PENDING_INTERRUPT : I8087_OK));
                            CHECK(eq(st(&f, result_slot), expected));
                            CHECK(f.last_gap == 0);
                            CHECK((f.status & 0x3f) == expected_flags);
                            CHECK((f.status & (I8087_IR | I8087_BUSY)) ==
                                  (trap ? (I8087_IR | I8087_BUSY) : 0));
                            CHECK(i8087_top(&f) == (form == 5 ? 1u : 0u));
                            CHECK(i8087_tag(&f, result_slot) ==
                                  (unsigned)(finite ? I8087_TAG_VALID : I8087_TAG_SPECIAL));
                            CHECK(writes == count);
                        }
    for (width = 32; width <= 64; width += 32)
        for (pc = 0; pc < 4; ++pc)
            for (sign = 0; sign < 2; ++sign)
                for (rc = 0; rc < 3; ++rc)
                    for (trap = 0; trap < 2; ++trap)
                        for (sticky = 0; sticky < 2; ++sticky)
                            for (popping = 0; popping < 2; ++popping) {
                                i8087 f, before;
                                bool finite = (sign && rc == 2) || (!sign && rc == 1);
                                uint64_t expected =
                                    width == 32
                                        ? (finite ? UINT64_C(0x7f7fffff) : UINT64_C(0x7f800000))
                                        : (finite ? UINT64_C(0x7fefffffffffffff)
                                                  : UINT64_C(0x7ff0000000000000));
                                uint16_t expected_flags =
                                    (uint16_t)(I8087_PE | ((rc == 0 || sticky) ? I8087_OE : 0));
                                unsigned count = writes;
                                expected |= (uint64_t)sign << (width - 1);
                                i8087_init(&f);
                                number(&f, 0, (i8087_f80){JBIT, (uint16_t)(0x6000 | (sign << 15))});
                                f.control = (uint16_t)((0x00bfu | (pc << 8) | (rc << 10)) &
                                                       ~(trap ? (unsigned)I8087_PE : 0u));
                                f.status = (uint16_t)(sticky ? I8087_OE : 0);
                                before = f;
                                memset(memory, 0x5a, 10);
                                CHECK(op(&f, (uint8_t)(width == 32 ? 0xd9 : 0xdd),
                                         (uint8_t)(popping ? 0x1e : 0x16)) ==
                                      (trap ? I8087_PENDING_INTERRUPT : I8087_OK));
                                CHECK(f.last_gap == 0);
                                CHECK((f.status & 0x3f) == expected_flags);
                                CHECK(i8087_top(&f) == popping);
                                CHECK(writes == count + 1);
                                for (i = 0; i < width / 8; ++i) {
                                    CHECK(memory[i] == (uint8_t)expected);
                                    expected >>= 8;
                                }
                                CHECK(memory[width / 8] == 0x5a);
                                CHECK(eq(f.registers[0], before.registers[0]));
                                CHECK(i8087_tag(&f, 0) ==
                                      (unsigned)(popping ? I8087_TAG_EMPTY : I8087_TAG_VALID));
                                /* Unmasked memory overflow still suppresses both store and pop. */
                                f = before;
                                f.control &= (uint16_t)~(I8087_OE | I8087_PE);
                                memset(memory, 0x5a, 10);
                                count = writes;
                                CHECK(op(&f, (uint8_t)(width == 32 ? 0xd9 : 0xdd),
                                         (uint8_t)(popping ? 0x1e : 0x16)) ==
                                      I8087_PENDING_INTERRUPT);
                                CHECK(f.last_gap == 0);
                                CHECK((f.status & 0x3f) == I8087_OE);
                                CHECK(same_registers(&f, &before));
                                CHECK(writes == count);
                                for (i = 0; i < 10; ++i)
                                    CHECK(memory[i] == 0x5a);
                            }
}

static void exponential_signed_zero(void) {
    unsigned sign, pc, rc, top, sticky, masks, full, p;
    for (sign = 0; sign < 2; ++sign)
        for (pc = 0; pc < 4; ++pc)
            for (rc = 0; rc < 4; ++rc)
                for (top = 0; top < 8; ++top)
                    for (sticky = 0; sticky < 2; ++sticky)
                        for (masks = 0; masks < 2; ++masks)
                            for (full = 0; full < 2; ++full) {
                                i8087 f, before;
                                i8087_f80 zero = {0, (uint16_t)(sign << 15)};
                                unsigned count = writes;
                                i8087_init(&f);
                                for (p = 0; p < 8; ++p)
                                    f.registers[p] = (i8087_f80){JBIT + p, 0x3fff};
                                f.tags = full ? 0 : 0xffff;
                                f.control = (uint16_t)((0x0080u | (pc << 8) | (rc << 10)) |
                                                       (masks ? 0x3fu : 0u));
                                f.status = (uint16_t)((top << 11) | CC | (sticky ? 0x3f : 0));
                                slot(&f, 0, zero, I8087_TAG_ZERO);
                                before = f;
                                CHECK(op(&f, 0xd9, 0xf0) == I8087_OK);
                                CHECK(f.last_gap == 0);
                                CHECK(f.used_approximations == 0);
                                CHECK(same_registers(&f, &before));
                                CHECK((f.status & 0x3f) == (before.status & 0x3f));
                                CHECK(!(f.status & (I8087_IR | I8087_BUSY)));
                                CHECK(f.control == before.control);
                                CHECK(writes == count);
                                CHECK(!i8087_interrupt_asserted(&f));
                            }
    /* A pseudozero or a mismatched tag does not establish the decoded exit. */
    for (sign = 0; sign < 2; ++sign) {
        i8087 f, before;
        i8087_init(&f);
        slot(&f, 0, (i8087_f80){0, (uint16_t)(0x4000 | (sign << 15))}, I8087_TAG_VALID);
        before = f;
        CHECK(op(&f, 0xd9, 0xf0) == I8087_UNVERIFIED);
        CHECK(same_registers(&f, &before));
        i8087_init(&f);
        slot(&f, 0, (i8087_f80){0, (uint16_t)(sign << 15)}, I8087_TAG_VALID);
        before = f;
        CHECK(op(&f, 0xd9, 0xf0) == I8087_UNVERIFIED);
        CHECK(same_registers(&f, &before));
        i8087_init(&f);
        slot(&f, 0, (i8087_f80){JBIT, (uint16_t)(0x3ffd | (sign << 15))}, I8087_TAG_ZERO);
        before = f;
        CHECK(op(&f, 0xd9, 0xf0) == I8087_UNVERIFIED);
        CHECK(same_registers(&f, &before));
    }
}

static void alternate_register_encodings(void) {
    enum { STORE_POP, EXCHANGE, COMPARE, COMPARE_POP, FREE_POP };

    /* Intel Table A-2, S-112..S-114: the nine starred encoding groups. */
    static const struct {
        uint8_t esc, base, canonical_esc, canonical_base;
        unsigned kind;
    } forms[] = {{0xd9, 0xd8, 0xdd, 0xd8, STORE_POP},   {0xdc, 0xd0, 0xd8, 0xd0, COMPARE},
                 {0xdc, 0xd8, 0xd8, 0xd8, COMPARE_POP}, {0xdd, 0xc8, 0xd9, 0xc8, EXCHANGE},
                 {0xde, 0xd0, 0xd8, 0xd8, COMPARE_POP}, {0xdf, 0xc0, 0, 0, FREE_POP},
                 {0xdf, 0xc8, 0xd9, 0xc8, EXCHANGE},    {0xdf, 0xd0, 0xdd, 0xd8, STORE_POP},
                 {0xdf, 0xd8, 0xdd, 0xd8, STORE_POP}};

    unsigned form, top, i, state, mode, trap, policy, k;
    for (form = 0; form < sizeof(forms) / sizeof(forms[0]); ++form)
        for (top = 0; top < 8; ++top)
            for (i = 0; i < 8; ++i)
                for (state = 0; state < 8; ++state)
                    for (mode = 0; mode < 16; ++mode)
                        for (trap = 0; trap < 3; ++trap)
                            for (policy = 0; policy < 2; ++policy) {
                                i8087 f, before, canonical, expected;
                                i8087_result result, want;
                                unsigned kind = forms[form].kind, dest = (top + i) & 7;
                                i8087_init(&f);
                                f.status = (uint16_t)((top << 11) | CC);
                                f.control =
                                    (uint16_t)(0x10bf | ((mode & 3) << 8) | ((mode >> 2) << 10));
                                if (trap)
                                    f.control &= (uint16_t)~(trap == 1 ? I8087_IE : I8087_DE);
                                i8087_set_policy(&f, policy ? I8087_ALLOW_APPROXIMATION
                                                            : I8087_STOP_ON_UNVERIFIED);
                                for (k = 0; k < 8; ++k)
                                    number(&f, k, integer((int)k + 1));
                                switch (state) {
                                    case 1:
                                        slot(&f, 0, f.registers[top], I8087_TAG_EMPTY);
                                        break;
                                    case 2:
                                        slot(&f, i, f.registers[dest], I8087_TAG_EMPTY);
                                        break;
                                    case 3:
                                        slot(&f, 0,
                                             (i8087_f80){UINT64_C(0x8123456789abcdef), 0xffff},
                                             I8087_TAG_VALID);
                                        if (i)
                                            slot(&f, i, (i8087_f80){123, 0xc123}, I8087_TAG_ZERO);
                                        break;
                                    case 4:
                                        number(&f, 0, (i8087_f80){1, 0});
                                        break;
                                    case 5:
                                        number(&f, 0,
                                               (i8087_f80){UINT64_C(0xc123456789abcdef), 0x7fff});
                                        break;
                                    case 6:
                                        number(&f, 0, (i8087_f80){0, 0x8000});
                                        number(&f, i, (i8087_f80){0, 0});
                                        break;
                                    case 7:
                                        f.status |= 0x3f;
                                        break;
                                    default:
                                        break;
                                }
                                before = canonical = expected = f;
                                result = op(&f, forms[form].esc, (uint8_t)(forms[form].base + i));
                                CHECK(result != I8087_BAD_OPCODE);
                                if (kind == FREE_POP) {
                                    /* Both ST(i) and old ST(0) become empty; FINCSTP alone cannot
                                       replace the second tag write. Recovered words #0074..0077. */
                                    expected.tags |=
                                        (uint16_t)((3u << (2 * dest)) | (3u << (2 * top)));
                                    expected.status = (uint16_t)((expected.status & 0xc7ffu) |
                                                                 (((top + 1) & 7) << 11));
                                    CHECK(result == I8087_OK);
                                    CHECK(same_registers(&f, &expected));
                                    CHECK(f.status == expected.status);
                                    CHECK(f.last_gap == 0);
                                } else {
                                    want = op(&canonical, forms[form].canonical_esc,
                                              (uint8_t)(forms[form].canonical_base + i));
                                    CHECK(result == want);
                                    CHECK(same_registers(&f, &canonical));
                                    CHECK(f.status == canonical.status);
                                    CHECK(f.control == canonical.control);
                                    CHECK(f.last_gap == canonical.last_gap);
                                    CHECK(f.used_approximations == canonical.used_approximations);
                                    CHECK(f.cpu_waiting == canonical.cpu_waiting);
                                    if (state == 0) {
                                        if (kind == STORE_POP) {
                                            slot(&expected, i, before.registers[top],
                                                 I8087_TAG_VALID);
                                            expected.tags |= (uint16_t)(3u << (2 * top));
                                            expected.status = (uint16_t)((before.status & 0xc7ffu) |
                                                                         (((top + 1) & 7) << 11));
                                        } else if (kind == EXCHANGE) {
                                            expected.registers[top] = before.registers[dest];
                                            expected.registers[dest] = before.registers[top];
                                        } else {
                                            expected.status =
                                                (uint16_t)((before.status &
                                                            ~(I8087_C0 | I8087_C2 | I8087_C3)) |
                                                           (i ? I8087_C0 : I8087_C3));
                                            if (kind == COMPARE_POP) {
                                                expected.tags |= (uint16_t)(3u << (2 * top));
                                                expected.status =
                                                    (uint16_t)((expected.status & 0xc7ffu) |
                                                               (((top + 1) & 7) << 11));
                                            }
                                        }
                                        CHECK(result == I8087_OK);
                                        CHECK(same_registers(&f, &expected));
                                        CHECK(f.status == expected.status);
                                    }
                                }
                                CHECK(f.control == before.control);
                                CHECK(f.data_address == before.data_address);
                                if (result == I8087_UNVERIFIED) {
                                    CHECK(f.opcode == before.opcode);
                                    CHECK(f.instruction_address == before.instruction_address);
                                } else {
                                    CHECK(f.opcode == (uint16_t)(((forms[form].esc & 7u) << 8) |
                                                                 (forms[form].base + i)));
                                    CHECK(f.instruction_address == 0x12345);
                                }
                            }
    /* Adjacent reserved and later additions are not covered by the footnotes. */
    {
        static const uint8_t rejected[][2] = {{0xd9, 0xd1}, {0xd9, 0xd7}, {0xdd, 0xe0},
                                              {0xde, 0xd8}, {0xde, 0xda}, {0xdf, 0xe0},
                                              {0xdf, 0xff}};
        for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
            i8087 f, before;
            i8087_init(&f);
            before = f;
            CHECK(op(&f, rejected[i][0], rejected[i][1]) == I8087_BAD_OPCODE);
            CHECK(same_registers(&f, &before));
            CHECK(f.status == before.status);
        }
    }
}

static void unnormal_zero_arithmetic(void) {
    static const struct {
        uint8_t esc, base;
        unsigned negative_slot;
        bool destination_i, pop;
    } forms[] = {
        {0xd8, 0xc0, 2, false, false}, {0xd8, 0xe0, 1, false, false}, {0xd8, 0xe8, 0, false, false},
        {0xdc, 0xc0, 2, true, false},  {0xdc, 0xe0, 1, true, false},  {0xdc, 0xe8, 0, true, false},
        {0xde, 0xc0, 2, true, true},   {0xde, 0xe0, 1, true, true},   {0xde, 0xe8, 0, true, true}};

    static const i8087_f80 raw[] = {{1, 0x0041},
                                    {UINT64_C(0x7fffffffffffffff), 0x0100},
                                    {UINT64_C(0x0123456789abcdef), 0x3fff},
                                    {UINT64_C(0x4000000000000001), 0x4000},
                                    {1, 0x403f},
                                    {UINT64_C(0x7fffffffffffffff), 0x7ffd}};
    unsigned form, item, top, i, which, sign, zero_sign, rc, masks, sticky, policy, k;
    for (form = 0; form < sizeof(forms) / sizeof(forms[0]); ++form)
        for (item = 0; item < sizeof(raw) / sizeof(raw[0]); ++item)
            for (top = 0; top < 8; ++top)
                for (i = 1; i < 8; ++i)
                    for (which = 0; which < 2; ++which)
                        for (sign = 0; sign < 2; ++sign)
                            for (zero_sign = 0; zero_sign < 2; ++zero_sign)
                                for (rc = 0; rc < 4; ++rc)
                                    for (masks = 0; masks < 2; ++masks)
                                        for (sticky = 0; sticky < 2; ++sticky)
                                            for (policy = 0; policy < 2; ++policy) {
                                                i8087 f, expected;
                                                i8087_f80 x = raw[item], want;
                                                unsigned dest = forms[form].destination_i ? i : 0;
                                                unsigned count = writes;
                                                x.sign_exponent |= (uint16_t)(sign << 15);
                                                i8087_init(&f);
                                                f.control = (uint16_t)(0x1380 | (rc << 10) |
                                                                       (masks ? 0x3f : 0));
                                                f.status = (uint16_t)((top << 11) | CC |
                                                                      (sticky ? 0x3f : 0));
                                                i8087_set_policy(&f,
                                                                 policy ? I8087_ALLOW_APPROXIMATION
                                                                        : I8087_STOP_ON_UNVERIFIED);
                                                for (k = 0; k < 8; ++k)
                                                    number(&f, k, integer((int)k + 11));
                                                slot(&f, which ? i : 0, x, I8087_TAG_VALID);
                                                slot(&f, which ? 0 : i,
                                                     (i8087_f80){0, (uint16_t)(zero_sign << 15)},
                                                     I8087_TAG_ZERO);
                                                expected = f;
                                                want = x;
                                                if (forms[form].negative_slot == which)
                                                    want.sign_exponent ^= 0x8000;
                                                slot(&expected, dest, want, I8087_TAG_VALID);
                                                expected.status &= (uint16_t)~I8087_C1;
                                                if (forms[form].pop) {
                                                    expected.tags |= (uint16_t)(3u << (2 * top));
                                                    expected.status =
                                                        (uint16_t)((expected.status & 0xc7ffu) |
                                                                   (((top + 1) & 7) << 11));
                                                }
                                                CHECK(op(&f, forms[form].esc,
                                                         (uint8_t)(forms[form].base + i)) ==
                                                      I8087_OK);
                                                CHECK(same_registers(&f, &expected));
                                                CHECK(f.status == expected.status);
                                                CHECK(f.control == expected.control);
                                                CHECK(f.last_gap == 0);
                                                CHECK(f.used_approximations == 0);
                                                CHECK(!i8087_interrupt_asserted(&f));
                                                CHECK(writes == count);
                                            }
    /* Memory zero has the same alignment path; no store or pop occurs. */
    {
        static const uint8_t escapes[] = {0xd8, 0xdc, 0xda, 0xde};
        unsigned group;
        for (form = 0; form < sizeof(escapes); ++form)
            for (group = 0; group < 3; ++group)
                for (sign = 0; sign < 2; ++sign)
                    for (zero_sign = 0; zero_sign < 2; ++zero_sign)
                        for (rc = 0; rc < 4; ++rc) {
                            i8087 f, expected;
                            i8087_f80 x = raw[2];
                            uint8_t modrm = (uint8_t)(group == 0 ? 0x06 : group == 1 ? 0x26 : 0x2e);
                            x.sign_exponent |= (uint16_t)(sign << 15);
                            i8087_init(&f);
                            f.control = (uint16_t)(0x03bf | (rc << 10));
                            f.status = CC | I8087_PE;
                            number(&f, 0, x);
                            memset(memory, 0, 8);
                            if (escapes[form] == 0xd8)
                                memory[3] = (uint8_t)(zero_sign << 7);
                            else if (escapes[form] == 0xdc)
                                memory[7] = (uint8_t)(zero_sign << 7);
                            expected = f;
                            if (group == 2)
                                x.sign_exponent ^= 0x8000;
                            slot(&expected, 0, x, I8087_TAG_VALID);
                            expected.status &= (uint16_t)~I8087_C1;
                            ok(&f, escapes[form], modrm);
                            CHECK(same_registers(&f, &expected));
                            CHECK(f.status == expected.status);
                        }
    }
    /* A denormal memory load retains its before-operation exception handling. */
    for (form = 0; form < 2; ++form)
        for (masks = 0; masks < 2; ++masks) {
            i8087 f, before;
            i8087_init(&f);
            slot(&f, 0, (i8087_f80){0, 0}, I8087_TAG_ZERO);
            if (!masks)
                f.control &= (uint16_t)~I8087_DE;
            memset(memory, 0, 8);
            memory[0] = 1;
            before = f;
            CHECK(op(&f, form ? 0xdc : 0xd8, 0x06) ==
                  (masks ? I8087_UNVERIFIED : I8087_PENDING_INTERRUPT));
            CHECK(same_registers(&f, &before));
            if (masks) {
                CHECK(f.status == before.status);
                CHECK(f.last_gap & I8087_GAP_UNNORMAL);
            } else {
                CHECK((f.status & 0x3f) == I8087_DE);
                CHECK((f.status & (I8087_IR | I8087_BUSY)) == (I8087_IR | I8087_BUSY));
                CHECK(f.last_gap == 0);
            }
        }
    /* Reduced/reserved PC, range edges, pseudozero and wrong tags stay guarded. */
    for (item = 0; item < 9; ++item) {
        i8087 f, before;
        i8087_f80 x = raw[2];
        i8087_init(&f);
        if (item < 3)
            f.control = (uint16_t)(0x00bf | (item << 8));
        else if (item == 3)
            x.sign_exponent = 0x0040;
        else if (item == 4)
            x.sign_exponent = 0x7ffe;
        else if (item == 5)
            x.significand = 0;
        else if (item == 6)
            x.sign_exponent = 0;
        slot(&f, 0, x, item == 7 ? I8087_TAG_SPECIAL : I8087_TAG_VALID);
        slot(&f, 1, (i8087_f80){0, 0}, item == 8 ? I8087_TAG_VALID : I8087_TAG_ZERO);
        before = f;
        CHECK(op(&f, 0xd8, 0xc1) == I8087_UNVERIFIED);
        CHECK(same_registers(&f, &before));
        CHECK(f.status == before.status);
        CHECK(f.last_gap & I8087_GAP_UNNORMAL);
    }
}

int main(void) {
    extract_fields();
    remainder_flags();
    scale_early_exit_and_fields();
    tangent_tiny();
    pseudozeros_and_tags();
    exceptions_and_interrupt_latch();
    range_destinations_and_nan_signs();
    round_integer_raw_encodings();
    masked_directed_overflow();
    exponential_signed_zero();
    alternate_register_encodings();
    unnormal_zero_arithmetic();
    printf(
        "PASS: %lu original-8087 regression assertions (document-derived, not hardware captures)\n",
        checks);
    return 0;
}
