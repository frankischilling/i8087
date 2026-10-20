/* SPDX-License-Identifier: MIT */
#ifndef I8087_H
#define I8087_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This is a value type, NOT a packed memory image. Use the encode/decode APIs. */
typedef struct i8087_f80 {
    uint64_t significand;
    uint16_t sign_exponent;
} i8087_f80;

enum {
    I8087_IE = 0x0001, I8087_DE = 0x0002, I8087_ZE = 0x0004,
    I8087_OE = 0x0008, I8087_UE = 0x0010, I8087_PE = 0x0020,
    I8087_IR = 0x0080, I8087_C0 = 0x0100, I8087_C1 = 0x0200,
    I8087_C2 = 0x0400, I8087_TOP = 0x3800, I8087_C3 = 0x4000,
    I8087_BUSY = 0x8000,
    I8087_INTERRUPT_MASK = 0x0080, I8087_AFFINE = 0x1000
};

enum {
    I8087_TAG_VALID = 0, I8087_TAG_ZERO = 1,
    I8087_TAG_SPECIAL = 2, I8087_TAG_EMPTY = 3
};

typedef enum i8087_result {
    I8087_OK = 0,
    I8087_PENDING_INTERRUPT,
    I8087_UNVERIFIED,
    I8087_BAD_OPCODE,
    I8087_MEMORY_FAULT,
    I8087_BAD_ARGUMENT
} i8087_result;

/* STOP is a guard against KNOWN gaps, not a claim of hardware certification. */
typedef enum i8087_policy {
    I8087_STOP_ON_UNVERIFIED = 0,
    I8087_ALLOW_APPROXIMATION = 1
} i8087_policy;

enum {
    I8087_GAP_TRANSCENDENTAL = 1u << 0,
    I8087_GAP_UNNORMAL = 1u << 1,
    I8087_GAP_RANGE_EXCEPTION = 1u << 2,
    I8087_GAP_EXCEPTION_RESTART = 1u << 3,
    I8087_GAP_PARTIAL_REMAINDER = 1u << 4,
    I8087_GAP_CONSTANT_ROM = 1u << 5,
    I8087_GAP_RESERVED_PRECISION = 1u << 6,
    I8087_GAP_UNDEFINED_OPERAND = 1u << 7,
    I8087_GAP_STACK_FAULT = 1u << 8
};

typedef struct i8087 {
    i8087_f80 registers[8];  /* Physical registers; ST(i) = (TOP+i)&7. */
    uint16_t control, status, tags, opcode;
    uint32_t instruction_address, data_address; /* 20-bit physical addresses. */
    i8087_policy policy;
    uint32_t last_gap, used_approximations;
    bool cpu_waiting;
} i8087;

/*
 * A callback is one complete operand transfer (at most 94 bytes).
 * Address arithmetic is physical and wraps at 20 bits, not at 64 KiB.
 * On false, a write MUST have no externally visible effect. Reads should be
 * side-effect-free if the caller wants to retry a rejected instruction.
 * Do not recursively execute the same i8087 instance from a callback.
 */
typedef struct i8087_bus {
    void *context;
    bool (*read)(void *, uint32_t, uint8_t *, size_t);
    bool (*write)(void *, uint32_t, const uint8_t *, size_t);
} i8087_bus;

/* Deterministic cold start. FNINIT, unlike cold start, preserves data/pointers. */
void i8087_init(i8087 *fpu);
void i8087_set_policy(i8087 *fpu, i8087_policy policy);

/*
 * Execute an ESC opcode D8..DF and its ModR/M byte. The HOST 8086/8088 decodes
 * displacement/prefixes and supplies the effective physical memory address.
 * No 8086 integer instructions, instruction prefetch, bus timing or pin timing
 * are simulated. A preceding 9B WAIT is handled by i8087_wait(), separately.
 */
i8087_result i8087_execute(i8087 *fpu, const i8087_bus *bus,
                          uint8_t escape, uint8_t modrm,
                          uint32_t instruction_address,
                          uint32_t effective_address);

i8087_result i8087_wait(i8087 *fpu);
bool i8087_interrupt_asserted(const i8087 *fpu);
unsigned i8087_top(const i8087 *fpu);
unsigned i8087_tag(const i8087 *fpu, unsigned logical_index);
bool i8087_get_st(const i8087 *fpu, unsigned logical_index, i8087_f80 *value);

void i8087_encode_f80(uint8_t bytes[10], i8087_f80 value);
i8087_f80 i8087_decode_f80(const uint8_t bytes[10]);
const char *i8087_result_string(i8087_result result);

#ifdef __cplusplus
}
#endif
#endif
