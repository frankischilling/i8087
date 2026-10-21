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
    if (value.significand && !(value.sign_exponent & 0x7fff))
        tag = I8087_TAG_SPECIAL;
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
    CHECK(i8087_interrupt_asserted(f) == i8087_interrupt_asserted(expected));
}

static void literal_matrix(void) {
    /* Raw results follow the original stored-count reconstruction and Intel
       Table 4b. These are source-derived literals, not captured chip output. */
    static const struct {
        i8087_f80 a, b, result;
        unsigned quotient, width;
        bool partial;
    } cases[] = {
        {{JBIT, 0x403f}, {UINT64_C(0xc000000000000000), 0x3fff}, {JBIT, 0x3fff}, 5, 3, true},
        {{JBIT + 1, 0x4040},
         {JBIT + 3, 0x3fff},
         {UINT64_C(0xc000000000000000), 0x3fc4},
         6,
         3,
         true},
        {{UINT64_MAX, 0x407f},
         {JBIT + 1, 0x3fff},
         {UINT64_C(0xc000000000000000), 0x4002},
         5,
         3,
         true},
        {{JBIT + 123, 0x403e}, {JBIT, 0x3fff}, {0, 0}, 3, 3, false},
        {{8, 0x403f}, {JBIT + 1, 0x3fff}, {UINT64_C(0xfffffffffffffff2), 0x3fff}, 7, 3, true},
        {{1, 0x403e}, {JBIT, 0x3fff}, {0, 0}, 1, 1, false},
        {{JBIT / 4, 0x4000}, {JBIT, 0x3fff}, {JBIT, 0x3ffe}, 0, 1, false},
        {{JBIT / 4, 0x3ffe}, {JBIT, 0x3fff}, {JBIT, 0x3ffc}, 0, 1, false},
        {{JBIT, 0x403f}, {JBIT, 0x3fff}, {0, 0}, 0, 3, true},
        {{4, 0x403f}, {JBIT + 1, 0x3fff}, {UINT64_C(0xfffffffffffffffa), 0x3fff}, 3, 2, true},
        {{1, 0x4041}, {JBIT, 0x3fff}, {0, 0}, 1, 1, true},
        {{0, 0x3fff}, {JBIT, 0x3fff}, {0, 0}, 0, 1, false},
        {{0, 0x3ffe}, {JBIT, 0x3fff}, {0, 0}, 0, 1, false},
        {{0, 0x403f}, {JBIT, 0x3fff}, {0, 0}, 0, 1, true},
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
                                            slot(&expected, 0, result);
                                            if (cases[item].width < 3)
                                                bits = ((history << cases[item].width) | bits) & 7;
                                            expected.status =
                                                (uint16_t)((expected.status & ~CC) |
                                                           condition_bits(bits) |
                                                           (cases[item].partial ? I8087_C2 : 0));
                                            expected.instruction_address = 0x12345;
                                            expected.opcode = 0x1f8;
                                            CHECK(i8087_execute(&f, NULL, 0xd9, 0xf8, 0x12345, 0) ==
                                                  I8087_OK);
                                            same_state(&f, &expected);
                                            CHECK(f.instruction_address ==
                                                  expected.instruction_address);
                                            CHECK(f.opcode == expected.opcode);
                                            CHECK(f.last_gap == 0);
                                            CHECK(f.used_approximations == 0);
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

static void tiny_result_metadata(void) {
    original_remainder_result result;
    CHECK(original_remainder((i8087_f80){1, 1}, (i8087_f80){JBIT, 1}, &result) ==
          ORIGINAL_REMAINDER_RANGE);
    CHECK(result.value.significand == JBIT);
    CHECK(result.exponent == -62);
    CHECK(!result.partial && result.quotient == 0 && result.width == 1);
}

static void tiny_result_matrix(void) {
    static const struct {
        i8087_f80 a, b;
        uint64_t denormal, normalized;
        uint16_t wrapped_exponent;
        unsigned quotient, width;
        bool partial;
    } cases[] = {
        {{JBIT + 1, 1}, {JBIT, 1}, 1, JBIT, 0x5fc2, 1, 1, false},
        {{UINT64_MAX, 1}, {JBIT, 1}, JBIT - 1, UINT64_MAX - 1, 0x6000, 1, 1, false},
        {{1, 1}, {JBIT, 1}, 1, JBIT, 0x5fc2, 0, 1, false},
        {{JBIT - 1, 1}, {JBIT, 2}, JBIT - 1, UINT64_MAX - 1, 0x6000, 0, 1, false},
        {{5, 65}, {UINT64_C(0x9fffffffffffffff), 1}, 8, JBIT, 0x5fc5, 4, 3, true},
    };

    unsigned item, signs, pc, rc, top, history, sticky, masks, interrupt_mask, policy;
    for (item = 0; item < sizeof(cases) / sizeof(cases[0]); ++item)
        for (signs = 0; signs < 4; ++signs)
            for (pc = 0; pc < 4; ++pc)
                for (rc = 0; rc < 4; ++rc)
                    for (top = 0; top < 8; ++top)
                        for (history = 0; history < 8; ++history)
                            for (sticky = 0; sticky < 2; ++sticky)
                                for (masks = 0; masks < 2; ++masks)
                                    for (interrupt_mask = 0; interrupt_mask < 2; ++interrupt_mask)
                                        for (policy = 0; policy < 2; ++policy) {
                                            i8087 f, expected;
                                            i8087_f80 a = cases[item].a, b = cases[item].b;
                                            unsigned i, bits = cases[item].quotient;
                                            a.sign_exponent |= (uint16_t)((signs & 1) << 15);
                                            b.sign_exponent |= (uint16_t)((signs & 2) << 14);
                                            i8087_init(&f);
                                            f.control = (uint16_t)(0x1000 | (pc << 8) | (rc << 10) |
                                                                   (masks ? 0x3f : 0) |
                                                                   (interrupt_mask ? 0x80 : 0));
                                            f.status =
                                                (uint16_t)((top << 11) | condition_bits(history) |
                                                           I8087_C2 | (sticky ? 0x3f : 0));
                                            f.policy = policy ? I8087_ALLOW_APPROXIMATION
                                                              : I8087_STOP_ON_UNVERIFIED;
                                            for (i = 0; i < 8; ++i)
                                                slot(&f, i, (i8087_f80){JBIT + i, 0x4003});
                                            slot(&f, 0, a);
                                            slot(&f, 1, b);
                                            expected = f;
                                            slot(&expected, 0,
                                                 (i8087_f80){
                                                     masks ? cases[item].denormal
                                                           : cases[item].normalized,
                                                     (uint16_t)(((signs & 1) << 15) |
                                                                (masks ? 0
                                                                       : cases[item]
                                                                             .wrapped_exponent))});
                                            if (cases[item].width < 3)
                                                bits = ((history << cases[item].width) | bits) & 7;
                                            expected.status =
                                                (uint16_t)((expected.status & ~CC) |
                                                           condition_bits(bits) | I8087_UE |
                                                           (cases[item].partial ? I8087_C2 : 0) |
                                                           (masks ? 0 : I8087_IR | I8087_BUSY));
                                            CHECK(i8087_execute(&f, NULL, 0xd9, 0xf8, 0x12345, 0) ==
                                                  (masks ? I8087_OK : I8087_PENDING_INTERRUPT));
                                            same_state(&f, &expected);
                                            CHECK(!f.last_gap && !f.used_approximations);
                                        }
}

static void paired_history_paths(void) {
    static const struct {
        uint64_t input, fraction;
        uint16_t exponent;
        unsigned quotient, width;
    } cases[] = {{0, 0, 0, 0, 1}, {1, JBIT, 0x4001, 0, 1}, {2, JBIT, 0x4000, 1, 1},
                 {3, 0, 0, 2, 2}, {4, JBIT, 0x4001, 2, 2}, {5, JBIT, 0x4000, 3, 2}};

    static const uint16_t divisor_exponents[] = {0x4001, 0x4000, 0x3fff, 0x3fc1, 0x3fc0};
    unsigned item, distance, signs, pc, rc, top, history, old_c2, sticky;
    for (item = 0; item < 6; ++item)
        for (distance = 0; distance < 5; ++distance)
            for (signs = 0; signs < 4; ++signs)
                for (pc = 0; pc < 4; ++pc)
                    for (rc = 0; rc < 4; ++rc)
                        for (top = 0; top < 8; ++top)
                            for (history = 0; history < 8; ++history)
                                for (old_c2 = 0; old_c2 < 2; ++old_c2)
                                    for (sticky = 0; sticky < 2; ++sticky) {
                                        i8087 f, expected;
                                        unsigned i, bits = ((history << cases[item].width) |
                                                            cases[item].quotient) &
                                                           7;
                                        i8087_init(&f);
                                        f.control = (uint16_t)(0x103f | (pc << 8) | (rc << 10));
                                        f.status =
                                            (uint16_t)((top << 11) | condition_bits(history) |
                                                       (old_c2 ? I8087_C2 : 0) |
                                                       (sticky ? 0x3f : 0));
                                        for (i = 0; i < 8; ++i)
                                            slot(&f, i, (i8087_f80){JBIT + i, 0x4003});
                                        slot(&f, 0,
                                             (i8087_f80){cases[item].input,
                                                         (uint16_t)(0x4040 | ((signs & 1) << 15))});
                                        slot(&f, 1,
                                             (i8087_f80){UINT64_C(0xc000000000000000),
                                                         (uint16_t)(divisor_exponents[distance] |
                                                                    ((signs & 2) << 14))});
                                        expected = f;
                                        slot(&expected, 0,
                                             (i8087_f80){cases[item].fraction,
                                                         (uint16_t)(cases[item].exponent |
                                                                    ((signs & 1) << 15))});
                                        expected.status = (uint16_t)((expected.status & ~CC) |
                                                                     condition_bits(bits) |
                                                                     (distance ? I8087_C2 : 0));
                                        CHECK(i8087_execute(&f, NULL, 0xd9, 0xf8, 0x12345, 0) ==
                                              I8087_OK);
                                        same_state(&f, &expected);
                                        CHECK(!f.last_gap && !f.used_approximations);
                                    }
}

int main(void) {
    literal_matrix();
    repeated_reduction();
    tiny_result_metadata();
    tiny_result_matrix();
    paired_history_paths();
    printf("PASS: %lu original remainder assertions (source-derived, not chip captures)\n", checks);
    return 0;
}
