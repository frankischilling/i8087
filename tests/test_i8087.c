/* SPDX-License-Identifier: MIT */
#include "i8087.h"
#include "soft80.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint8_t memory[1u << 20];
static bool fail_read, fail_write;
static unsigned writes;
static bool read_memory(void *ctx, uint32_t address, uint8_t *out, size_t n)
{
    size_t i;
    (void)ctx;
    if (fail_read)
        return false;
    for (i = 0; i < n; ++i)
        out[i] = memory[(address + (uint32_t)i) & 0xfffff];
    return true;
}
static bool write_memory(void *ctx, uint32_t address, const uint8_t *in, size_t n)
{
    size_t i;
    (void)ctx;
    if (fail_write)
        return false;
    ++writes;
    for (i = 0; i < n; ++i)
        memory[(address + (uint32_t)i) & 0xfffff] = in[i];
    return true;
}
static const i8087_bus bus = {NULL, read_memory, write_memory};
static void put(uint32_t address, uint64_t v, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        memory[(address + (uint32_t)i) & 0xfffff] = (uint8_t)v;
        v >>= 8;
    }
}
static uint64_t get(uint32_t address, size_t n)
{
    uint64_t v = 0;
    size_t i;
    for (i = 0; i < n; ++i)
        v |= (uint64_t)memory[(address + (uint32_t)i) & 0xfffff] << (8 * i);
    return v;
}
static i8087_result exec(i8087 *f, uint8_t esc, uint8_t m, uint32_t ea)
{
    return i8087_execute(f, &bus, esc, m, 0xabcde, ea);
}
static void ok(i8087 *f, uint8_t esc, uint8_t m, uint32_t ea)
{
    CHECK(exec(f, esc, m, ea) == I8087_OK);
}
static bool equal(i8087_f80 a, i8087_f80 b)
{
    return a.significand == b.significand && a.sign_exponent == b.sign_exponent;
}
static i8087_f80 st(const i8087 *f, unsigned i)
{
    i8087_f80 x;
    CHECK(i8087_get_st(f, i, &x));
    return x;
}
static void load80(i8087 *f, i8087_f80 x)
{
    i8087_encode_f80(memory + 0x100, x);
    ok(f, 0xdb, 0x2e, 0x100);
}
static void loadint(i8087 *f, int64_t n)
{
    put(0x100, (uint64_t)n, 8);
    ok(f, 0xdf, 0x2e, 0x100);
}
static void expectint(const i8087 *f, unsigned i, int64_t n)
{
    CHECK(equal(st(f, i), sf_from_signed_bits((uint64_t)n, 64)));
}
static bool same_arch(const i8087 *a, const i8087 *b)
{
    unsigned i;
    if (a->control != b->control || a->status != b->status || a->tags != b->tags ||
        a->opcode != b->opcode || a->instruction_address != b->instruction_address ||
        a->data_address != b->data_address)
        return false;
    for (i = 0; i < 8; ++i)
        if (!equal(a->registers[i], b->registers[i]))
            return false;
    return true;
}
static void test_basics(void)
{
    i8087 f, other;
    unsigned i;
    i8087_init(&f);
    CHECK(f.control == 0x03bf);
    CHECK(f.status == 0);
    CHECK(f.tags == 0xffff);
    CHECK(i8087_wait(&f) == I8087_OK);
    CHECK(!i8087_interrupt_asserted(&f));
    loadint(&f, 2);
    loadint(&f, 3);
    ok(&f, 0xde, 0xc1, 0);
    expectint(&f, 0, 5);
    loadint(&f, 4);
    ok(&f, 0xde, 0xc9, 0);
    expectint(&f, 0, 20);
    ok(&f, 0xdb, 0x1e, 0x80);
    CHECK(get(0x80, 4) == 20);
    CHECK(f.tags == 0xffff);
    for (i = 0; i < 8; ++i)
        loadint(&f, (int64_t)i + 1);
    CHECK(i8087_top(&f) == 0);
    for (i = 0; i < 8; ++i)
        expectint(&f, i, 8 - (int64_t)i);
    other = f;
    CHECK(exec(&f, 0xd9, 0xe8, 0) == I8087_UNVERIFIED);
    CHECK(same_arch(&f, &other));
    CHECK(f.last_gap & I8087_GAP_STACK_FAULT);
    ok(&f, 0xdd, 0xc0, 0);
    CHECK(i8087_tag(&f, 0) == I8087_TAG_EMPTY);
    ok(&f, 0xd9, 0xf7, 0);
    expectint(&f, 0, 7);
    ok(&f, 0xd9, 0xf6, 0);
    CHECK(i8087_tag(&f, 0) == I8087_TAG_EMPTY);
    other = f;
    ok(&f, 0xdb, 0xe3, 0);
    CHECK(f.tags == 0xffff);
    CHECK(i8087_top(&f) == 0);
    CHECK(f.instruction_address == other.instruction_address);
    CHECK(f.opcode == other.opcode);
    for (i = 0; i < 8; ++i)
        CHECK(equal(f.registers[i], other.registers[i]));
    i8087_init(&f);
    loadint(&f, 7);
    loadint(&f, 9);
    ok(&f, 0xd9, 0xc1, 0);
    expectint(&f, 0, 7);
    expectint(&f, 1, 9);
    expectint(&f, 2, 7);
    ok(&f, 0xd9, 0xc9, 0);
    expectint(&f, 0, 9);
    expectint(&f, 1, 7);
    ok(&f, 0xdd, 0xda, 0);
    expectint(&f, 0, 7);
    expectint(&f, 1, 9);
}
static void test_arithmetic_order(void)
{
    const struct {
        uint8_t esc, m;
        int64_t want;
        unsigned dest;
    } cases[] = {{0xd8, 0xe1, 6, 0}, {0xd8, 0xe9, -6, 0}, {0xd8, 0xf1, 4, 0},
                 {0xdc, 0xe1, 6, 1}, {0xdc, 0xe9, -6, 1}, {0xdc, 0xf1, 4, 1},
                 {0xde, 0xe1, 6, 0}, {0xde, 0xe9, -6, 0}, {0xde, 0xf1, 4, 0}};
    size_t i;
    i8087 f;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        i8087_init(&f);
        loadint(&f, 2);
        loadint(&f, 8);
        ok(&f, cases[i].esc, cases[i].m, 0);
        expectint(&f, cases[i].dest, cases[i].want);
    }
    i8087_init(&f);
    loadint(&f, 2);
    loadint(&f, 8);
    ok(&f, 0xd8, 0xf9, 0);
    CHECK(equal(st(&f, 0), (i8087_f80){UINT64_C(0x8000000000000000), 0x3ffd}));
    i8087_init(&f);
    loadint(&f, 8);
    loadint(&f, 2);
    ok(&f, 0xdc, 0xf9, 0);
    expectint(&f, 1, 4);
    i8087_init(&f);
    loadint(&f, 8);
    loadint(&f, 2);
    ok(&f, 0xde, 0xf9, 0);
    expectint(&f, 0, 4);
    i8087_init(&f);
    loadint(&f, 8);
    put(0x80, 2, 2);
    ok(&f, 0xde, 0x26, 0x80);
    expectint(&f, 0, 6);
    put(0x80, 0x40000000, 4);
    ok(&f, 0xd8, 0x0e, 0x80);
    expectint(&f, 0, 12);
    put(0x80, 0x4000000000000000ull, 8);
    ok(&f, 0xdc, 0x36, 0x80);
    expectint(&f, 0, 6);
    put(0x80, 4, 4);
    ok(&f, 0xda, 0x06, 0x80);
    expectint(&f, 0, 10);
}
static void test_numeric_ops(void)
{
    i8087 f;
    unsigned rc;
    for (rc = 0; rc < 4; ++rc) {
        const int64_t expected[4] = {2, 1, 2, 1};
        i8087_init(&f);
        f.control = (uint16_t)((f.control & ~0xc00u) | (rc << 10));
        load80(&f, (i8087_f80){UINT64_C(0xc000000000000000), 0x3fff});
        ok(&f, 0xd9, 0xfc, 0);
        expectint(&f, 0, expected[rc]);
        CHECK(f.status & I8087_PE);
    }
    i8087_init(&f);
    loadint(&f, 81);
    ok(&f, 0xd9, 0xfa, 0);
    expectint(&f, 0, 9);
    ok(&f, 0xd9, 0xe0, 0);
    expectint(&f, 0, -9);
    ok(&f, 0xd9, 0xe1, 0);
    expectint(&f, 0, 9);
    ok(&f, 0xd9, 0xf4, 0);
    CHECK(equal(st(&f, 0), (i8087_f80){UINT64_C(0x9000000000000000), 0x3fff}));
    expectint(&f, 1, 3);
    ok(&f, 0xd9, 0xfd, 0);
    expectint(&f, 0, 9);
    i8087_init(&f);
    loadint(&f, -3);
    loadint(&f, 64);
    ok(&f, 0xd9, 0xfd, 0);
    expectint(&f, 0, 8);
    i8087_init(&f);
    loadint(&f, 4);
    loadint(&f, 18);
    ok(&f, 0xd9, 0xf8, 0);
    expectint(&f, 0, 2);
    CHECK(!f.last_gap);
    CHECK(!f.used_approximations);
}
static void test_transcendental_guards(void)
{
    i8087 f, saved;
    unsigned i;
    const uint8_t opcodes[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf9};
    for (i = 0; i < sizeof(opcodes); ++i) {
        i8087_init(&f);
        loadint(&f, 0);
        load80(&f, (i8087_f80){UINT64_C(0x8000000000000000), 0x3ffd});
        saved = f;
        CHECK(exec(&f, 0xd9, opcodes[i], 0) == I8087_UNVERIFIED);
        CHECK(same_arch(&f, &saved));
        CHECK(f.last_gap & I8087_GAP_TRANSCENDENTAL);
        i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
        ok(&f, 0xd9, opcodes[i], 0);
        CHECK(f.used_approximations & I8087_GAP_TRANSCENDENTAL);
    }
    i8087_init(&f);
    i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
    loadint(&f, 3);
    loadint(&f, 2);
    ok(&f, 0xd9, 0xf1, 0);
    expectint(&f, 0, 3);
    i8087_init(&f);
    i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
    loadint(&f, 0);
    ok(&f, 0xd9, 0xf2, 0);
    expectint(&f, 0, 1);
    expectint(&f, 1, 0);
    i8087_init(&f);
    CHECK(exec(&f, 0xd9, 0xeb, 0) == I8087_UNVERIFIED);
    CHECK(f.last_gap & I8087_GAP_CONSTANT_ROM);
    i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
    ok(&f, 0xd9, 0xeb, 0);
    CHECK(st(&f, 0).significand == UINT64_C(0xc90fdaa22168c235));
    i8087_init(&f);
    i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
    loadint(&f, 1);
    CHECK(exec(&f, 0xd9, 0xf0, 0) == I8087_UNVERIFIED);
    CHECK(f.last_gap & I8087_GAP_UNDEFINED_OPERAND);
}
static void test_specials(void)
{
    i8087 f;
    const i8087_f80 inf = {UINT64_C(0x8000000000000000), 0x7fff};
    const i8087_f80 nan = {UINT64_C(0xc123456789abcdef), 0x7fff};
    i8087_init(&f);
    load80(&f, nan);
    CHECK(!(f.status & I8087_IE));
    ok(&f, 0xd9, 0xe5, 0);
    CHECK((f.status & (I8087_C0 | I8087_C2 | I8087_C3)) == I8087_C0);
    loadint(&f, 2);
    ok(&f, 0xd8, 0xc1, 0);
    CHECK(f.status & I8087_IE);
    CHECK(equal(st(&f, 0), nan));
    i8087_init(&f);
    load80(&f, inf);
    load80(&f, (i8087_f80){inf.significand, 0xffff});
    ok(&f, 0xd8, 0xd1, 0);
    CHECK(f.status & I8087_C3);
    CHECK(!(f.status & I8087_IE));
    f.control |= I8087_AFFINE;
    ok(&f, 0xd8, 0xd1, 0);
    CHECK(f.status & I8087_C0);
    CHECK(!(f.status & I8087_C3));
    i8087_init(&f);
    loadint(&f, 2);
    load80(&f, inf);
    ok(&f, 0xd8, 0xd1, 0);
    CHECK((f.status & 0x4501) == 0x4501);
    i8087_init(&f);
    load80(&f, inf);
    ok(&f, 0xd9, 0xfa, 0);
    CHECK(f.status & I8087_IE);
    CHECK(equal(st(&f, 0), sf_indefinite()));
    i8087_init(&f);
    f.control |= I8087_AFFINE;
    load80(&f, inf);
    ok(&f, 0xd9, 0xfa, 0);
    CHECK(equal(st(&f, 0), inf));
    i8087_init(&f);
    load80(&f, inf);
    ok(&f, 0xd8, 0xc0, 0);
    CHECK(f.status & I8087_IE);
    i8087_init(&f);
    loadint(&f, 0);
    loadint(&f, 1);
    ok(&f, 0xd8, 0xf1, 0);
    CHECK(f.status & I8087_ZE);
    CHECK(equal(st(&f, 0), inf));
    i8087_init(&f);
    loadint(&f, 5);
    loadint(&f, 5);
    ok(&f, 0xde, 0xd9, 0);
    CHECK(f.status & I8087_C3);
    CHECK(f.tags == 0xffff);
    i8087_init(&f);
    ok(&f, 0xd9, 0xe5, 0);
    CHECK((f.status & 0x4500) == 0x4100);
    load80(&f, (i8087_f80){0, 0x8000});
    ok(&f, 0xd9, 0xe5, 0);
    CHECK((f.status & 0x4700) == 0x4200);
}
static void test_memory_and_unnormals(void)
{
    i8087 f;
    const i8087_f80 raw = {UINT64_C(0x123456789abcdef0), 0x3456};
    i8087_init(&f);
    put(0x80, 0x3fc00000, 4);
    ok(&f, 0xd9, 0x06, 0x80);
    ok(&f, 0xdd, 0x1e, 0x90);
    CHECK(get(0x90, 8) == UINT64_C(0x3ff8000000000000));
    put(0x90, UINT64_C(0xc002000000000000), 8);
    ok(&f, 0xdd, 0x06, 0x90);
    ok(&f, 0xd9, 0x1e, 0x80);
    CHECK(get(0x80, 4) == UINT32_C(0xc0100000));
    loadint(&f, INT64_MIN);
    ok(&f, 0xdf, 0x3e, 0x80);
    CHECK(get(0x80, 8) == UINT64_C(0x8000000000000000));
    load80(&f, raw);
    ok(&f, 0xdb, 0x3e, 0x90);
    CHECK(equal(i8087_decode_f80(memory + 0x90), raw));
    put(0x80, 1, 4);
    ok(&f, 0xd9, 0x06, 0x80);
    CHECK(f.status & I8087_DE);
    CHECK(st(&f, 0).sign_exponent == 0x3f81);
    CHECK(st(&f, 0).significand == UINT64_C(1) << 40);
    ok(&f, 0xd9, 0xe5, 0);
    CHECK((f.status & 0x4500) == 0);
    ok(&f, 0xdf, 0x1e, 0x90);
    CHECK(f.status & I8087_IE);
    CHECK(get(0x90, 2) == 0x8000);
    i8087_init(&f);
    load80(&f, raw);
    loadint(&f, 1);
    ok(&f, 0xd8, 0xf1, 0);
    CHECK(f.status & I8087_IE);
    i8087_init(&f);
    put(0xffffe, 0x12345678, 4);
    ok(&f, 0xdb, 0x06, 0xffffe);
    expectint(&f, 0, 0x12345678);
    ok(&f, 0xdb, 0x1e, 0xffff);
    CHECK(get(0xffff, 4) == 0x12345678); /* Does not wrap at 64 KiB. */
}
static void test_bcd(void)
{
    i8087 f;
    unsigned i;
    i8087_init(&f);
    for (i = 0; i < 9; ++i)
        memory[0x200 + i] = 0x99;
    memory[0x209] = 0x80;
    ok(&f, 0xdf, 0x26, 0x200);
    expectint(&f, 0, -INT64_C(999999999999999999));
    ok(&f, 0xdf, 0x36, 0x300);
    CHECK(!memcmp(memory + 0x200, memory + 0x300, 10));
    memset(memory + 0x200, 0, 10);
    memory[0x209] = 0x80;
    ok(&f, 0xdf, 0x26, 0x200);
    CHECK(st(&f, 0).sign_exponent == 0x8000);
    ok(&f, 0xdf, 0x36, 0x300);
    CHECK(!memcmp(memory + 0x200, memory + 0x300, 10));
    memory[0x200] = 0xfa;
    CHECK(exec(&f, 0xdf, 0x26, 0x200) == I8087_UNVERIFIED);
    CHECK(f.last_gap & I8087_GAP_UNDEFINED_OPERAND);
}
static void test_environments(void)
{
    i8087 f, before;
    unsigned i;
    uint16_t cw;
    i8087_init(&f);
    loadint(&f, 17);
    loadint(&f, 33);
    loadint(&f, 44);
    f.instruction_address = 0xabcde;
    f.data_address = 0xdef01;
    f.opcode = 0x123;
    f.control &= (uint16_t)~(I8087_INTERRUPT_MASK | I8087_ZE);
    cw = f.control;
    ok(&f, 0xd9, 0x36, 0x200);
    CHECK(get(0x200, 2) == cw);
    CHECK((f.control & 0x3f) == 0x3f);
    CHECK(!(f.control & I8087_INTERRUPT_MASK));
    CHECK(get(0x206, 2) == 0xbcde);
    CHECK(get(0x208, 2) == 0xa123);
    CHECK(get(0x20a, 2) == 0xef01);
    CHECK(get(0x20c, 2) == 0xd000);
    ok(&f, 0xd9, 0x26, 0x200);
    CHECK(f.control == cw);
    before = f;
    ok(&f, 0xdd, 0x36, 0x300);
    CHECK(f.tags == 0xffff);
    CHECK(i8087_top(&f) == 0);
    for (i = 0; i < 8; ++i)
        CHECK(equal(f.registers[i], before.registers[i]));
    CHECK(equal(i8087_decode_f80(memory + 0x30e), st(&before, 0)));
    ok(&f, 0xdd, 0x26, 0x300);
    CHECK(same_arch(&f, &before));
    ok(&f, 0xd9, 0x3e, 0x400);
    CHECK(get(0x400, 2) == f.control);
    ok(&f, 0xdd, 0x3e, 0x410);
    CHECK(get(0x410, 2) == f.status);
    put(0x420, 0x1bbf, 2);
    ok(&f, 0xd9, 0x2e, 0x420);
    CHECK(f.control == 0x1bbf);
}
static void test_interrupts(void)
{
    i8087 f, before;
    i8087_init(&f);
    loadint(&f, 0);
    loadint(&f, 1);
    f.control &= (uint16_t)~I8087_ZE;
    before = f;
    CHECK(exec(&f, 0xd8, 0xf1, 0) == I8087_PENDING_INTERRUPT);
    CHECK(!f.last_gap);
    CHECK(f.tags == before.tags);
    expectint(&f, 0, 1);
    CHECK((f.status & (I8087_ZE | I8087_IR | I8087_BUSY)) == (I8087_ZE | I8087_IR | I8087_BUSY));
    CHECK(!i8087_interrupt_asserted(&f));
    CHECK(i8087_wait(&f) == I8087_PENDING_INTERRUPT);
    CHECK(i8087_interrupt_asserted(&f));
    CHECK(exec(&f, 0xd9, 0xe8, 0) == I8087_PENDING_INTERRUPT);
    ok(&f, 0xdb, 0xe0, 0);
    CHECK(i8087_interrupt_asserted(&f));
    ok(&f, 0xdb, 0xe1, 0);
    CHECK(!i8087_interrupt_asserted(&f));
    ok(&f, 0xdb, 0xe2, 0);
    CHECK(!(f.status & (0xbf | I8087_BUSY)));
    CHECK(!i8087_interrupt_asserted(&f));
    CHECK(i8087_wait(&f) == I8087_OK);
    i8087_init(&f);
    i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
    loadint(&f, 40000);
    f.control &= (uint16_t)~I8087_IE;
    put(0x80, 12345, 2);
    CHECK(exec(&f, 0xdf, 0x1e, 0x80) == I8087_PENDING_INTERRUPT);
    CHECK(get(0x80, 2) == 12345);
    expectint(&f, 0, 40000);
}
static void test_failure_atomicity(void)
{
    i8087 f, before;
    unsigned old_writes;
    i8087_init(&f);
    loadint(&f, 123);
    before = f;
    fail_write = true;
    old_writes = writes;
    CHECK(exec(&f, 0xdb, 0x1e, 0x80) == I8087_MEMORY_FAULT);
    CHECK(same_arch(&f, &before));
    CHECK(writes == old_writes);
    fail_write = false;
    fail_read = true;
    CHECK(exec(&f, 0xdb, 0x06, 0x80) == I8087_MEMORY_FAULT);
    CHECK(same_arch(&f, &before));
    fail_read = false;
    CHECK(i8087_execute(&f, NULL, 0xdb, 0x06, 0, 0) == I8087_MEMORY_FAULT);
    CHECK(i8087_execute(NULL, &bus, 0xd8, 0xc0, 0, 0) == I8087_BAD_ARGUMENT);
    CHECK(exec(&f, 0x90, 0xc0, 0) == I8087_BAD_ARGUMENT);
    CHECK(exec(&f, 0xd9, 0xfe, 0) == I8087_BAD_OPCODE); /* FSIN: not 8087 */
    CHECK(exec(&f, 0xd9, 0xfb, 0) == I8087_BAD_OPCODE); /* FSINCOS */
    CHECK(exec(&f, 0xd9, 0xf5, 0) == I8087_BAD_OPCODE); /* FPREM1 */
    CHECK(exec(&f, 0xdf, 0xe0, 0) == I8087_BAD_OPCODE); /* FNSTSW AX */
    CHECK(exec(&f, 0xdb, 0xe4, 0) == I8087_BAD_OPCODE); /* FSETPM */
    CHECK(exec(&f, 0xdd, 0xe0, 0) == I8087_BAD_OPCODE); /* FUCOM */
    CHECK(same_arch(&f, &before));
}
static void test_decoder_smoke(void)
{
    unsigned esc, m, i;
    i8087 f;
    for (esc = 0xd8; esc <= 0xdf; ++esc)
        for (m = 0; m < 256; ++m) {
            i8087_result r;
            i8087_init(&f);
            i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
            for (i = 0; i < 4; ++i)
                loadint(&f, (int64_t)i + 1);
            memset(memory + 0x500, 0, 94);
            r = exec(&f, (uint8_t)esc, (uint8_t)m, 0x500);
            CHECK(r >= I8087_OK && r <= I8087_BAD_ARGUMENT);
            CHECK(!(f.status & 0x0040));
        }
}
int main(void)
{
    test_basics();
    test_arithmetic_order();
    test_numeric_ops();
    test_transcendental_guards();
    test_specials();
    test_memory_and_unnormals();
    test_bcd();
    test_environments();
    test_interrupts();
    test_failure_atomicity();
    test_decoder_smoke();
    printf("PASS: %u instruction/API assertions (including all 2048 ESC/ModR/M pairs)\n", checks);
    return 0;
}
