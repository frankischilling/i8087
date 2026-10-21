/* SPDX-License-Identifier: MIT */
#include "i8087.h"
#include "original_add.h"
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

static void slot(i8087 *f, unsigned i, i8087_f80 value) {
    unsigned physical = (i8087_top(f) + i) & 7;
    f->registers[physical] = value;
    f->tags &= (uint16_t)~(3u << (2 * physical));
    if (!value.significand && !(value.sign_exponent & 0x7fff))
        f->tags |= (uint16_t)(1u << (2 * physical));
}

static bool equal(i8087_f80 a, i8087_f80 b) {
    return a.sign_exponent == b.sign_exponent && a.significand == b.significand;
}

static void same_storage(const i8087 *f, const i8087 *expected) {
    unsigned i;
    for (i = 0; i < 8; ++i)
        CHECK(equal(f->registers[i], expected->registers[i]));
    CHECK(f->tags == expected->tags);
    CHECK(f->control == expected->control);
    CHECK(f->status == expected->status);
}

static void literal_instruction_matrix(void) {
    static const struct {
        i8087_f80 a, b, result;
        unsigned precision_field, rc;
        bool inexact, increment;
    } cases[] = {
        {{UINT64_C(0x2000000000000000), 0x4000},
         {UINT64_C(0x2000000000000000), 0x4000},
         {UINT64_C(0x4000000000000000), 0x4000},
         3,
         0,
         false,
         false},
        {{UINT64_C(0x4000000000000000), 0x4000},
         {UINT64_C(0x4000000000000000), 0x4000},
         {JBIT, 0x4000},
         3,
         0,
         false,
         false},
        {{UINT64_C(0xc000000000000000), 0x4000},
         {UINT64_C(0x6000000000000000), 0x4000},
         {UINT64_C(0x9000000000000000), 0x4001},
         3,
         0,
         false,
         false},
        {{UINT64_C(0x6000000000000000), 0x4000},
         {UINT64_C(0x2000000000000000), 0xc000},
         {UINT64_C(0x4000000000000000), 0x4000},
         3,
         0,
         false,
         false},
        {{JBIT, 0x4000},
         {UINT64_C(0x4000000000000000), 0xc000},
         {JBIT, 0x3fff},
         3,
         0,
         false,
         false},
        {{UINT64_C(0x6000000000000000), 0x4001},
         {JBIT, 0xbfff},
         {UINT64_C(0x4000000000000000), 0x4001},
         3,
         0,
         false,
         false},
        {{UINT64_C(0x1000000000000000), 0x4001},
         {JBIT, 0xbfff},
         {UINT64_C(0x1000000000000000), 0xc001},
         3,
         0,
         false,
         false},
        {{1, 0x4000}, {3, 0xbfff}, {0, 0xc000}, 3, 0, true, false},
        {{1, 0x4000}, {3, 0xbfff}, {1, 0xc000}, 3, 1, true, true},
        {{1, 0x4000}, {3, 0xbfff}, {0, 0xc000}, 3, 2, true, false},
        {{1, 0x4000}, {1, 0x3fff}, {2, 0x4000}, 3, 0, true, true},
        {{2, 0x4000}, {1, 0x3fff}, {2, 0x4000}, 3, 0, true, false},
        {{1, 0x403e}, {JBIT, 0x3fff}, {2, 0x403e}, 3, 0, false, false},
        {{1, 0x403e}, {JBIT + 1, 0xbfff}, {0, 0xc03e}, 3, 0, true, false},
        {{UINT64_C(0x4000007fffffffff), 0x4000},
         {1, 0x4000},
         {UINT64_C(0x4000000000000000), 0x4000},
         0,
         0,
         true,
         false},
        {{UINT64_C(0x4000017fffffffff), 0x4000},
         {1, 0x4000},
         {UINT64_C(0x4000020000000000), 0x4000},
         0,
         0,
         true,
         true},
        {{UINT64_C(0x7fffff7fffffffff), 0x4000}, {1, 0x4000}, {JBIT, 0x4000}, 0, 0, true, true},
        {{UINT64_C(0x7ffffffffffffbff), 0x4000}, {1, 0x4000}, {JBIT, 0x4000}, 2, 0, true, true},
        {{JBIT - 1, 0x4000}, {JBIT - 1, 0x4000}, {JBIT, 0x4001}, 2, 0, true, true},
        {{JBIT - 1, 0x4000}, {JBIT - 1, 0x4000}, {JBIT, 0x4001}, 0, 0, true, true},
        {{1, 0x4000}, {1, 0xbfff}, {0, 0x4000}, 3, 0, true, false},
        {{1, 0x4000}, {1, 0xbfff}, {1, 0x4000}, 3, 2, true, true},
        {{1, 0x403f}, {JBIT, 0x3fff}, {2, 0x403f}, 3, 0, true, true},
        {{1, 0x403f}, {JBIT, 0xbfff}, {0, 0x403f}, 3, 0, true, false},
        {{1, 0x403f}, {JBIT, 0xbfff}, {1, 0x403f}, 3, 2, true, true},
        {{1, 0x4040}, {JBIT, 0x3fff}, {1, 0x4040}, 3, 0, true, false},
        {{1, 0x4040}, {JBIT, 0xbfff}, {1, 0x4040}, 3, 0, true, true},
        {{1, 0x4041}, {JBIT, 0x3fff}, {1, 0x4041}, 3, 0, true, false},
        {{1, 0x4041}, {JBIT, 0xbfff}, {1, 0x4041}, 3, 0, true, true},
        {{1, 0x7ffd}, {JBIT, 0x0041}, {1, 0x7ffd}, 3, 0, true, false},
        {{1, 0x7ffd}, {JBIT, 0x8041}, {1, 0x7ffd}, 3, 0, true, true},
        {{1, 0x4040}, {1, 0x4000}, {1, 0x4040}, 3, 0, true, false},
        {{1, 0x4040}, {1, 0xc000}, {1, 0x4040}, 3, 0, true, true},
        {{1, 0x403f}, {JBIT + 1, 0x3fff}, {2, 0x403f}, 3, 0, true, true},
        {{1, 0x403f}, {JBIT + 1, 0xbfff}, {0, 0x403f}, 3, 0, true, false},
        {{JBIT / 2, 0x4000}, {JBIT / 2, 0xc000}, {0, 0x4000}, 3, 0, false, false},
        {{JBIT / 2, 0x4000}, {JBIT / 2, 0xc000}, {0, 0xc000}, 3, 1, false, false},
        {{JBIT / 2, 0x4000}, {JBIT / 2, 0xc000}, {0, 0x4000}, 2, 2, false, false},
        {{JBIT / 2, 0x4000}, {JBIT / 2, 0xc000}, {0, 0x4000}, 0, 3, false, false},
        {{JBIT, 0x4000}, {JBIT, 0xc000}, {0, 0}, 3, 0, false, false},
        {{JBIT, 0x4000}, {JBIT, 0xc000}, {0, 0x8000}, 3, 1, false, false},
        {{1, 0x403e}, {JBIT, 0xbfff}, {0, 0x403e}, 2, 0, false, false},
        {{1, 0x403e}, {JBIT, 0xbfff}, {0, 0xc03e}, 0, 1, false, false},
        {{JBIT, 1}, {JBIT, 0x8001}, {0, 0}, 3, 0, false, false},
        {{JBIT, 0x7ffe}, {JBIT, 0xfffe}, {0, 0x8000}, 3, 1, false, false},
        {{JBIT / 2, 1}, {JBIT / 2, 0x8001}, {0, 1}, 2, 0, false, false},
        {{JBIT / 2, 0x7ffe}, {JBIT / 2, 0xfffe}, {0, 0xfffe}, 0, 1, false, false}};

    static const struct {
        uint8_t escape, base;
        unsigned negative_slot;
        bool destination_i, pop;
    } forms[] = {
        {0xd8, 0xc0, 2, false, false}, {0xd8, 0xe0, 1, false, false}, {0xd8, 0xe8, 0, false, false},
        {0xdc, 0xc0, 2, true, false},  {0xdc, 0xe0, 1, true, false},  {0xdc, 0xe8, 0, true, false},
        {0xde, 0xc0, 2, true, true},   {0xde, 0xe0, 1, true, true},   {0xde, 0xe8, 0, true, true}};

    unsigned item, form, top, i, which, sign, masks, sticky, policy, initial_c1, k;
    for (item = 0; item < sizeof(cases) / sizeof(cases[0]); ++item)
        for (form = 0; form < sizeof(forms) / sizeof(forms[0]); ++form)
            for (top = 0; top < 8; ++top)
                for (i = 1; i < 8; ++i)
                    for (which = 0; which < 2; ++which)
                        for (sign = 0; sign < 2; ++sign)
                            for (masks = 0; masks < 2; ++masks)
                                for (sticky = 0; sticky < 2; ++sticky)
                                    for (policy = 0; policy < 2; ++policy)
                                        for (initial_c1 = 0; initial_c1 < 2; ++initial_c1) {
                                            i8087 f, expected;
                                            i8087_f80 a = which ? cases[item].b : cases[item].a;
                                            i8087_f80 b = which ? cases[item].a : cases[item].b;
                                            i8087_f80 want = cases[item].result;
                                            unsigned destination =
                                                forms[form].destination_i ? i : 0;
                                            unsigned rc = cases[item].rc;
                                            bool interrupt = cases[item].inexact && !masks;
                                            bool cancellation =
                                                !want.significand && !cases[item].inexact;
                                            if (sign) {
                                                a.sign_exponent ^= 0x8000;
                                                b.sign_exponent ^= 0x8000;
                                                want.sign_exponent ^= 0x8000;
                                                if (rc == 1 || rc == 2)
                                                    rc = 3 - rc;
                                            }
                                            if (cancellation)
                                                want.sign_exponent =
                                                    (uint16_t)((want.sign_exponent & 0x7fff) |
                                                               (rc == 1 ? 0x8000 : 0));
                                            if (forms[form].negative_slot == 0)
                                                a.sign_exponent ^= 0x8000;
                                            if (forms[form].negative_slot == 1)
                                                b.sign_exponent ^= 0x8000;
                                            i8087_init(&f);
                                            f.control =
                                                (uint16_t)(0x1000 |
                                                           (cases[item].precision_field << 8) |
                                                           (rc << 10) | (masks ? 0x3f : 0));
                                            f.status =
                                                (uint16_t)((top << 11) | I8087_C0 | I8087_C2 |
                                                           I8087_C3 | (initial_c1 ? I8087_C1 : 0) |
                                                           (sticky ? 0x3f : 0));
                                            f.policy = policy ? I8087_ALLOW_APPROXIMATION
                                                              : I8087_STOP_ON_UNVERIFIED;
                                            f.instruction_address = 0x123;
                                            f.data_address = 0x456;
                                            for (k = 0; k < 8; ++k)
                                                slot(&f, k, (i8087_f80){JBIT + k, 0x4002});
                                            slot(&f, 0, a);
                                            slot(&f, i, b);
                                            expected = f;
                                            slot(&expected, destination, want);
                                            expected.status =
                                                (uint16_t)((expected.status & ~I8087_C1) |
                                                           (cases[item].increment ? I8087_C1 : 0) |
                                                           (cases[item].inexact ? I8087_PE : 0) |
                                                           (interrupt ? I8087_IR | I8087_BUSY : 0));
                                            if (cancellation)
                                                expected.status |= f.status & I8087_C1;
                                            if (forms[form].pop) {
                                                expected.tags |= (uint16_t)(3u << (2 * top));
                                                expected.status =
                                                    (uint16_t)((expected.status & 0xc7ffu) |
                                                               (((top + 1) & 7) << 11));
                                            }
                                            CHECK(i8087_execute(&f, NULL, forms[form].escape,
                                                                (uint8_t)(forms[form].base + i),
                                                                0xabcde, 0) ==
                                                  (interrupt ? I8087_PENDING_INTERRUPT : I8087_OK));
                                            same_storage(&f, &expected);
                                            CHECK(f.last_gap == 0);
                                            CHECK(f.used_approximations == 0);
                                            CHECK(i8087_interrupt_asserted(&f) == interrupt);
                                            CHECK(f.instruction_address == 0xabcde);
                                            CHECK(f.data_address == 0x456);
                                            CHECK(f.opcode ==
                                                  (uint16_t)(((forms[form].escape & 7u) << 8) |
                                                             (forms[form].base + i)));
                                        }
}

static void unresolved_boundaries(void) {
    unsigned pc, rc;
    for (pc = 0; pc < 4; ++pc)
        for (rc = 0; rc < 4; ++rc) {
            i8087 f, before;
            sf_result predicted;
            i8087_init(&f);
            f.control = (uint16_t)(0xbf | (pc << 8) | (rc << 10));
            f.status = CC;
            slot(&f, 0, (i8087_f80){JBIT >> 1, 0x4000});
            slot(&f, 1, (i8087_f80){JBIT >> 1, 0xc000});
            before = f;
            if (pc == 1) {
                CHECK(i8087_execute(&f, NULL, 0xd8, 0xc1, 0xabcde, 0) == I8087_UNVERIFIED);
                same_storage(&f, &before);
                CHECK(f.last_gap & I8087_GAP_RESERVED_PRECISION);
            } else {
                slot(&before, 0, (i8087_f80){0, (uint16_t)(0x4000 | (rc == 1 ? 0x8000 : 0))});
                CHECK(i8087_execute(&f, NULL, 0xd8, 0xc1, 0xabcde, 0) == I8087_OK);
                same_storage(&f, &before);
                CHECK(!f.last_gap && !f.used_approximations);
            }
            CHECK(original_add((i8087_f80){JBIT / 2, 0x4000}, (i8087_f80){JBIT / 2, 0xc000}, 64, rc,
                               &predicted) == ORIGINAL_ADD_C1_UNCHANGED);
            CHECK(equal(predicted.value,
                        (i8087_f80){0, (uint16_t)(0x4000 | (rc == 1 ? 0x8000 : 0))}));
        }
    /* A normal GRS-only half-bit follows #0280/#0281 and normalizes by 63. */
    {
        sf_result result;
        CHECK(original_add((i8087_f80){JBIT, 0x3fff}, (i8087_f80){UINT64_MAX, 0xbffe}, 64, 0,
                           &result) == ORIGINAL_ADD_COMPLETE);
        CHECK(equal(result.value, (i8087_f80){JBIT, 0x3fbf}));
        CHECK(!result.flags && !result.rounded_up);
        CHECK(original_add((i8087_f80){UINT64_MAX, 0x7ffe}, (i8087_f80){UINT64_MAX, 0x7ffe}, 24, 0,
                           &result) == ORIGINAL_ADD_RANGE);
    }
}

static void canonical_zero_paths(void) {
    static const uint8_t instructions[] = {0xc1, 0xe1, 0xe9};
    static const unsigned fields[] = {0, 2, 3};
    unsigned operation, signs, pc, rc, top, initial_c1, sticky, masks;
    for (operation = 0; operation < 3; ++operation)
        for (signs = 0; signs < 4; ++signs)
            for (pc = 0; pc < 3; ++pc)
                for (rc = 0; rc < 4; ++rc)
                    for (top = 0; top < 8; ++top)
                        for (initial_c1 = 0; initial_c1 < 2; ++initial_c1)
                            for (sticky = 0; sticky < 2; ++sticky)
                                for (masks = 0; masks < 2; ++masks) {
                                    i8087 f, expected;
                                    unsigned sign_a = signs & 1, sign_b = signs >> 1;
                                    unsigned i, result_sign;
                                    i8087_init(&f);
                                    f.control = (uint16_t)(0x1000 | (fields[pc] << 8) | (rc << 10) |
                                                           (masks ? 0x3f : 0));
                                    f.status = (uint16_t)((top << 11) | I8087_C0 | I8087_C2 |
                                                          I8087_C3 | (initial_c1 ? I8087_C1 : 0) |
                                                          (sticky ? 0x3f : 0));
                                    for (i = 0; i < 8; ++i)
                                        slot(&f, i, (i8087_f80){JBIT + i, 0x4003});
                                    slot(&f, 0, (i8087_f80){0, (uint16_t)(sign_a << 15)});
                                    slot(&f, 1, (i8087_f80){0, (uint16_t)(sign_b << 15)});
                                    if (operation == 1)
                                        sign_b ^= 1;
                                    else if (operation == 2) {
                                        unsigned temporary = sign_a;
                                        sign_a = sign_b;
                                        sign_b = temporary ^ 1;
                                    }
                                    result_sign = sign_a == sign_b ? sign_a : (unsigned)(rc == 1);
                                    expected = f;
                                    slot(&expected, 0,
                                         (i8087_f80){0, (uint16_t)(result_sign << 15)});
                                    CHECK(i8087_execute(&f, NULL, 0xd8, instructions[operation],
                                                        0xabcde, 0) == I8087_OK);
                                    same_storage(&f, &expected);
                                    CHECK(!f.last_gap && !f.used_approximations);
                                    CHECK(!i8087_interrupt_asserted(&f));
                                }
}

typedef struct memory_operand {
    uint64_t bits;
    unsigned reads;
} memory_operand;

static bool read_operand(void *context, uint32_t address, uint8_t *bytes, size_t length) {
    memory_operand *operand = context;
    size_t i;
    if (address != 0x23456 || length > 8)
        return false;
    ++operand->reads;
    for (i = 0; i < length; ++i)
        bytes[i] = (uint8_t)(operand->bits >> (8 * i));
    return true;
}

static void nonzero_memory_operands(void) {
    static const uint8_t escapes[] = {0xd8, 0xdc, 0xda, 0xde};
    static const uint64_t fractions[3][3] = {
        {JBIT, JBIT >> 1, JBIT >> 1},
        {UINT64_C(0x3000000000000000), UINT64_C(0x1000000000000000), UINT64_C(0x1000000000000000)},
        {JBIT, 0, 0}};
    static const unsigned negative[3][3] = {{0, 0, 1}, {0, 1, 0}, {0, 0, 0}};
    static const unsigned fields[] = {0, 2, 3};
    unsigned item, format, operation, sign, pc, rc, top;
    for (item = 0; item < 3; ++item)
        for (format = 0; format < 4; ++format)
            for (operation = 0; operation < 3; ++operation)
                for (sign = 0; sign < 2; ++sign)
                    for (pc = 0; pc < 3; ++pc)
                        for (rc = 0; rc < 4; ++rc)
                            for (top = 0; top < 8; ++top) {
                                memory_operand operand = {0, 0};
                                i8087_bus bus = {&operand, read_operand, NULL};
                                i8087 f, expected;
                                i8087_f80 want = {
                                    fractions[item][operation],
                                    (uint16_t)((item == 2 ? 0x4000 : 0x4001) |
                                               ((negative[item][operation] ^ sign) << 15))};
                                bool cancellation = item == 2 && operation != 0;
                                if (cancellation)
                                    want.sign_exponent =
                                        (uint16_t)(0x4000 | (rc == 1 ? 0x8000 : 0));
                                uint8_t modrm = operation == 0   ? 0x06
                                                : operation == 1 ? 0x26
                                                                 : 0x2e;
                                if (format == 0)
                                    operand.bits = UINT64_C(0x3f800000) | ((uint64_t)sign << 31);
                                else if (format == 1)
                                    operand.bits =
                                        UINT64_C(0x3ff0000000000000) | ((uint64_t)sign << 63);
                                else
                                    operand.bits = sign ? UINT64_MAX : 1;
                                i8087_init(&f);
                                f.control = (uint16_t)(0x1000 | (fields[pc] << 8) | (rc << 10));
                                f.status = (uint16_t)((top << 11) | CC | I8087_DE | I8087_PE);
                                slot(&f, 0,
                                     (i8087_f80){
                                         item == 2 ? JBIT / 2
                                         : item    ? UINT64_C(0x1000000000000000)
                                                   : UINT64_C(0x6000000000000000),
                                         (uint16_t)((item == 2 ? 0x4000 : 0x4001) | (sign << 15))});
                                expected = f;
                                slot(&expected, 0, want);
                                if (!cancellation)
                                    expected.status &= (uint16_t)~I8087_C1;
                                CHECK(i8087_execute(&f, &bus, escapes[format], modrm, 0xabcde,
                                                    0x23456) == I8087_OK);
                                same_storage(&f, &expected);
                                CHECK(f.last_gap == 0);
                                CHECK(!i8087_interrupt_asserted(&f));
                                CHECK(f.data_address == 0x23456);
                                CHECK(operand.reads == 1);
                            }
}

int main(void) {
    literal_instruction_matrix();
    unresolved_boundaries();
    canonical_zero_paths();
    nonzero_memory_operands();
    printf("PASS: %lu original-add assertions (source-derived, not hardware captures)\n", checks);
    return 0;
}
