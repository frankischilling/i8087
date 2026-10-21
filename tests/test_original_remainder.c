/* SPDX-License-Identifier: MIT */
#include "i8087.h"
#include "original_remainder.h"
#include <stdio.h>
#include <stdlib.h>

#define JBIT UINT64_C(0x8000000000000000)
#define CC (I8087_C0 | I8087_C1 | I8087_C2 | I8087_C3)
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned long checks;

static uint16_t condition_bits(unsigned history) {
    return (uint16_t)(((history & 4) ? I8087_C0 : 0) | ((history & 2) ? I8087_C3 : 0) |
                      ((history & 1) ? I8087_C1 : 0));
}

static void slot(i8087 *f, unsigned logical, i8087_f80 value) {
    unsigned physical = (i8087_top(f) + logical) & 7;
    unsigned tag =
        value.significand || (value.sign_exponent & 0x7fff) ? I8087_TAG_VALID : I8087_TAG_ZERO;
    f->registers[physical] = value;
    f->tags = (uint16_t)((f->tags & ~(3u << (2 * physical))) | (tag << (2 * physical)));
}

static void same_state(const i8087 *f, const i8087 *expected) {
    unsigned i;
    for (i = 0; i < 8; ++i) {
        CHECK(f->registers[i].significand == expected->registers[i].significand);
        CHECK(f->registers[i].sign_exponent == expected->registers[i].sign_exponent);
    }
    CHECK(f->tags == expected->tags);
    CHECK(f->control == expected->control);
    CHECK(f->status == expected->status);
    CHECK(!i8087_interrupt_asserted(f));
}

static void literal_matrix(void) {
    /* Raw results follow the original stored-count reconstruction and Intel
       Table 4b. These are source-derived literals, not captured chip output. */
    static const struct {
        i8087_f80 a, b, result;
        unsigned quotient, width;
        bool partial, history_guard;
    } cases[] = {
        {{JBIT, 0x403f}, {UINT64_C(0xc000000000000000), 0x3fff}, {JBIT, 0x3fff}, 5, 3, true, false},
        {{JBIT + 1, 0x4040},
         {JBIT + 3, 0x3fff},
         {UINT64_C(0xc000000000000000), 0x3fc4},
         6,
         3,
         true,
         false},
        {{UINT64_MAX, 0x407f},
         {JBIT + 1, 0x3fff},
         {UINT64_C(0xc000000000000000), 0x4002},
         5,
         3,
         true,
         false},
        {{JBIT + 123, 0x403e}, {JBIT, 0x3fff}, {0, 0}, 3, 3, false, false},
        {{8, 0x403f},
         {JBIT + 1, 0x3fff},
         {UINT64_C(0xfffffffffffffff2), 0x3fff},
         7,
         3,
         true,
         false},
        {{1, 0x403e}, {JBIT, 0x3fff}, {0, 0}, 1, 1, false, false},
        {{JBIT / 4, 0x4000}, {JBIT, 0x3fff}, {JBIT, 0x3ffe}, 0, 1, false, false},
        {{JBIT / 4, 0x3ffe}, {JBIT, 0x3fff}, {JBIT, 0x3ffc}, 0, 1, false, false},
        {{JBIT, 0x403f}, {JBIT, 0x3fff}, {0, 0}, 0, 3, true, false},
        {{4, 0x403f}, {JBIT + 1, 0x3fff}, {UINT64_C(0xfffffffffffffffa), 0x3fff}, 3, 2, true, true},
        {{1, 0x4041}, {JBIT, 0x3fff}, {0, 0}, 1, 1, true, true},
        {{0, 0x3fff}, {JBIT, 0x3fff}, {0, 0}, 0, 1, false, false},
        {{0, 0x3ffe}, {JBIT, 0x3fff}, {0, 0}, 0, 1, false, false},
        {{0, 0x403f}, {JBIT, 0x3fff}, {0, 0}, 0, 1, true, true},
    };

    unsigned item, signs, pc, rc, top, history, old_c2, sticky, masks, policy;
    for (item = 0; item < sizeof(cases) / sizeof(cases[0]); ++item)
        for (signs = 0; signs < 4; ++signs)
            for (pc = 0; pc < 4; ++pc)
                for (rc = 0; rc < 4; ++rc)
                    for (top = 0; top < 8; ++top)
                        for (history = 0; history < 8; ++history)
                            for (old_c2 = 0; old_c2 < 2; ++old_c2)
                                for (sticky = 0; sticky < 2; ++sticky)
                                    for (masks = 0; masks < 2; ++masks)
                                        for (policy = 0; policy < 2; ++policy) {
                                            i8087 f, expected;
                                            i8087_f80 a = cases[item].a, b = cases[item].b;
                                            i8087_f80 result = cases[item].result;
                                            unsigned i, bits = cases[item].quotient;
                                            bool blocked = cases[item].history_guard && !policy;
                                            a.sign_exponent |= (uint16_t)((signs & 1) << 15);
                                            b.sign_exponent |= (uint16_t)((signs & 2) << 14);
                                            result.sign_exponent |= (uint16_t)((signs & 1) << 15);
                                            i8087_init(&f);
                                            f.control = (uint16_t)(0x1000 | (pc << 8) | (rc << 10) |
                                                                   (masks ? 0x3f : 0));
                                            f.status =
                                                (uint16_t)((top << 11) | condition_bits(history) |
                                                           (old_c2 ? I8087_C2 : 0) |
                                                           (sticky ? 0x3f : 0));
                                            f.policy = policy ? I8087_ALLOW_APPROXIMATION
                                                              : I8087_STOP_ON_UNVERIFIED;
                                            for (i = 0; i < 8; ++i)
                                                slot(&f, i, (i8087_f80){JBIT + i, 0x4003});
                                            slot(&f, 0, a);
                                            slot(&f, 1, b);
                                            f.instruction_address = 0x54321;
                                            f.opcode = 0x123;
                                            expected = f;
                                            if (!blocked) {
                                                slot(&expected, 0, result);
                                                if (cases[item].history_guard)
                                                    bits = history;
                                                else if (cases[item].width < 3)
                                                    bits =
                                                        ((history << cases[item].width) | bits) & 7;
                                                expected.status =
                                                    (uint16_t)((expected.status & ~CC) |
                                                               condition_bits(bits) |
                                                               (cases[item].partial ? I8087_C2
                                                                                    : 0));
                                                expected.instruction_address = 0x12345;
                                                expected.opcode = 0x1f8;
                                            }
                                            CHECK(i8087_execute(&f, NULL, 0xd9, 0xf8, 0x12345, 0) ==
                                                  (blocked ? I8087_UNVERIFIED : I8087_OK));
                                            same_state(&f, &expected);
                                            CHECK(f.instruction_address ==
                                                  expected.instruction_address);
                                            CHECK(f.opcode == expected.opcode);
                                            CHECK(f.last_gap ==
                                                  (cases[item].history_guard
                                                       ? (uint32_t)I8087_GAP_PARTIAL_REMAINDER
                                                       : 0u));
                                            CHECK(f.used_approximations ==
                                                  (cases[item].history_guard && policy
                                                       ? (uint32_t)I8087_GAP_PARTIAL_REMAINDER
                                                       : 0u));
                                        }
}

static void repeated_reduction(void) {
    static const uint16_t exponents[] = {0x40c0, 0x4080, 0x4040, 0x4000, 0x4000};
    unsigned top, signs, pc, rc, history, step;
    for (top = 0; top < 8; ++top)
        for (signs = 0; signs < 4; ++signs)
            for (pc = 0; pc < 4; ++pc)
                for (rc = 0; rc < 4; ++rc)
                    for (history = 0; history < 8; ++history) {
                        i8087 f, expected;
                        unsigned i;
                        i8087_init(&f);
                        f.control = (uint16_t)(0x3f | (pc << 8) | (rc << 10));
                        f.status = (uint16_t)((top << 11) | condition_bits(history) | I8087_UE);
                        for (i = 0; i < 8; ++i)
                            slot(&f, i, (i8087_f80){JBIT + i, 0x4003});
                        slot(&f, 0, (i8087_f80){JBIT, (uint16_t)(0x4100 | ((signs & 1) << 15))});
                        slot(&f, 1,
                             (i8087_f80){UINT64_C(0xc000000000000000),
                                         (uint16_t)(0x4000 | ((signs & 2) << 14))});
                        expected = f;
                        for (step = 0; step < 5; ++step) {
                            slot(&expected, 0,
                                 (i8087_f80){JBIT,
                                             (uint16_t)(exponents[step] | ((signs & 1) << 15))});
                            expected.status = (uint16_t)((expected.status & ~CC) |
                                                         condition_bits(step < 4 ? 5 : 2) |
                                                         (step < 4 ? I8087_C2 : 0));
                            CHECK(i8087_execute(&f, NULL, 0xd9, 0xf8, 0x12345 + step, 0) ==
                                  I8087_OK);
                            same_state(&f, &expected);
                            CHECK(!f.last_gap && !f.used_approximations);
                        }
                    }
}

static void tiny_result_guard(void) {
    original_remainder_result result;
    i8087 f, before;
    CHECK(original_remainder((i8087_f80){1, 1}, (i8087_f80){JBIT, 1}, &result) ==
          ORIGINAL_REMAINDER_RANGE);
    i8087_init(&f);
    slot(&f, 0, (i8087_f80){1, 1});
    slot(&f, 1, (i8087_f80){JBIT, 1});
    before = f;
    CHECK(i8087_execute(&f, NULL, 0xd9, 0xf8, 0x12345, 0) == I8087_UNVERIFIED);
    same_state(&f, &before);
    CHECK(f.last_gap == I8087_GAP_RANGE_EXCEPTION);
    CHECK(!f.used_approximations);
}

int main(void) {
    literal_matrix();
    repeated_reduction();
    tiny_result_guard();
    printf("PASS: %lu original remainder assertions (source-derived, not chip captures)\n", checks);
    return 0;
}
