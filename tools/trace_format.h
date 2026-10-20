/* SPDX-License-Identifier: MIT */
#ifndef I8087_TRACE_FORMAT_H
#define I8087_TRACE_FORMAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TRACE_HEADER_SIZE 32u
#define TRACE_INPUT_SIZE 96u
#define TRACE_OUTPUT_SIZE 100u
#define TRACE_MAX_RECORDS 1000000u

static uint16_t trace_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t trace_u32(const uint8_t *p)
{
    return (uint32_t)trace_u16(p) | ((uint32_t)trace_u16(p + 2) << 16);
}

static void trace_put(uint8_t *p, uint32_t x, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        p[i] = (uint8_t)x;
        x >>= 8;
    }
}

/* Only documented 8087 register encodings. No FNSTSW AX or later opcodes. */
static bool trace_register_opcode(uint8_t esc, uint8_t m)
{
    unsigned g = (m >> 3) & 7;
    if (m < 0xc0)
        return false;
    switch (esc) {
        case 0xd8:
            return true;
        case 0xd9:
            return m <= 0xcf || m == 0xd0 || (m >= 0xd8 && m <= 0xdf) || m == 0xe0 || m == 0xe1 ||
                   m == 0xe4 || m == 0xe5 || (m >= 0xe8 && m <= 0xee) || (m >= 0xf0 && m <= 0xf4) ||
                   (m >= 0xf6 && m <= 0xfa) || m == 0xfc || m == 0xfd;
        case 0xdb:
            return m >= 0xe0 && m <= 0xe3;
        case 0xdc:
            return true;
        case 0xdd:
        case 0xdf:
            return g <= 3;
        case 0xde:
            return g != 3 || m == 0xd9;
        default:
            return false;
    }
}
#endif
