/* SPDX-License-Identifier: MIT
 * Deterministic raw-state/API safety stress test, NOT a hardware oracle. */
#include "i8087.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASES 50000u
static uint64_t rng = UINT64_C(0x8087198020260200);
static unsigned current_case;

static uint64_t random64(void)
{
    uint64_t x = rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng = x;
    return x;
}

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "stress case %u, line %d: %s\n", current_case, __LINE__, #x);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct memory {
    uint8_t bytes[512];
    unsigned reads, writes;
    bool fail_read, fail_write;
} memory;

static bool rd(void *ctx, uint32_t a, uint8_t *p, size_t n)
{
    memory *m = ctx;
    size_t i;
    ++m->reads;
    CHECK(a <= 0xfffff && n <= 94);
    if (m->fail_read)
        return false;
    for (i = 0; i < n; ++i)
        p[i] = m->bytes[(a + (uint32_t)i) & 511];
    return true;
}

static bool wr(void *ctx, uint32_t a, const uint8_t *p, size_t n)
{
    memory *m = ctx;
    size_t i;
    CHECK(a <= 0xfffff && n <= 94);
    if (m->fail_write)
        return false;
    for (i = 0; i < n; ++i)
        m->bytes[(a + (uint32_t)i) & 511] = p[i];
    ++m->writes;
    return true;
}

static bool same(const i8087 *a, const i8087 *b, bool omit_gap)
{
    unsigned i;
    for (i = 0; i < 8; ++i)
        if (a->registers[i].significand != b->registers[i].significand ||
            a->registers[i].sign_exponent != b->registers[i].sign_exponent)
            return false;
    return a->control == b->control && a->status == b->status && a->tags == b->tags &&
           a->opcode == b->opcode && a->instruction_address == b->instruction_address &&
           a->data_address == b->data_address && a->policy == b->policy &&
           (omit_gap || a->last_gap == b->last_gap) &&
           a->used_approximations == b->used_approximations && a->cpu_waiting == b->cpu_waiting;
}

static void seed(i8087 *f)
{
    static const uint16_t exps[] = {0,      1,      2,      0x3fbe, 0x3fbf, 0x3ffe,
                                    0x3fff, 0x4000, 0x7ffd, 0x7ffe, 0x7fff};
    unsigned i;
    i8087_init(f);
    f->control = (uint16_t)(random64() & 0x1fbf);
    f->status = (uint16_t)(random64() & 0xffbf);
    f->tags = (uint16_t)random64();
    f->instruction_address = (uint32_t)random64() & 0xfffff;
    f->data_address = (uint32_t)random64() & 0xfffff;
    f->opcode = (uint16_t)(random64() & 0x7ff);
    f->policy = (random64() & 1) ? I8087_STOP_ON_UNVERIFIED : I8087_ALLOW_APPROXIMATION;
    f->last_gap = (uint32_t)random64() & 0x1ff;
    f->used_approximations = (uint32_t)random64() & 0x1ff;
    f->cpu_waiting = (random64() & 15) == 0;
    for (i = 0; i < 8; ++i) {
        unsigned shape = (unsigned)random64() & 7;
        uint16_t sign = (random64() & 1) ? 0x8000 : 0;
        uint64_t sig = random64();
        if (shape == 0)
            sig = 0;
        else if (shape == 1)
            sig = 1;
        else if (shape == 2)
            sig = UINT64_MAX;
        else if (shape == 3)
            sig = UINT64_C(0x8000000000000000);
        else if (shape == 4)
            sig &= UINT64_C(0x7fffffffffffffff);
        else
            sig |= UINT64_C(0x8000000000000000);
        f->registers[i].significand = sig;
        f->registers[i].sign_exponent =
            (uint16_t)(sign | exps[random64() % (sizeof(exps) / sizeof(exps[0]))]);
    }
}

int main(void)
{
    unsigned outcomes[6] = {0};
    for (current_case = 0; current_case < CASES; ++current_case) {
        i8087 before, a, b;
        memory initial = {0}, ma, mb;
        size_t j;
        uint8_t esc = (uint8_t)(0xd8 + (random64() & 7)), modrm = (uint8_t)random64();
        uint32_t ip = (uint32_t)random64(), ea = (uint32_t)random64();
        i8087_result ra, rb;
        i8087_bus ba, bb;
        seed(&before);
        a = before;
        b = before;
        for (j = 0; j < sizeof(initial.bytes); ++j)
            initial.bytes[j] = (uint8_t)random64();
        initial.fail_read = (random64() % 19) == 0;
        initial.fail_write = (random64() % 17) == 0;
        ma = initial;
        mb = initial;
        ba = (i8087_bus){&ma, rd, wr};
        bb = (i8087_bus){&mb, rd, wr};
        ra = i8087_execute(&a, &ba, esc, modrm, ip, ea);
        rb = i8087_execute(&b, &bb, esc, modrm, ip, ea);
        CHECK(ra == rb);
        CHECK(ra >= I8087_OK && ra <= I8087_BAD_ARGUMENT);
        ++outcomes[(unsigned)ra];
        CHECK(same(&a, &b, false));
        CHECK(memcmp(ma.bytes, mb.bytes, sizeof(ma.bytes)) == 0);
        CHECK(ma.reads == mb.reads && ma.writes == mb.writes);
        CHECK(ma.reads <= 1 && ma.writes <= 1);
        CHECK(!(a.status & 0x40));
        CHECK(i8087_top(&a) < 8);
        if (ra == I8087_UNVERIFIED)
            CHECK(same(&a, &before, true));
        if (ra == I8087_BAD_OPCODE || ra == I8087_BAD_ARGUMENT || ra == I8087_MEMORY_FAULT)
            CHECK(same(&a, &before, false));
        if (ra != I8087_OK && ra != I8087_PENDING_INTERRUPT) {
            CHECK(ma.writes == 0);
            CHECK(memcmp(ma.bytes, initial.bytes, sizeof(ma.bytes)) == 0);
        }
        if (ra == I8087_OK || ra == I8087_PENDING_INTERRUPT)
            CHECK((a.used_approximations & before.used_approximations) ==
                  before.used_approximations);
    }
    printf(
        "PASS: %u deterministic raw-state cases, %u duplicate executions; safety invariants only\n",
        CASES, CASES * 2);
    printf("outcomes OK=%u pending=%u guarded=%u bad-opcode=%u memory-fault=%u bad-argument=%u\n",
           outcomes[0], outcomes[1], outcomes[2], outcomes[3], outcomes[4], outcomes[5]);
    return 0;
}
