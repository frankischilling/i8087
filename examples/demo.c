/* SPDX-License-Identifier: MIT */
#include "i8087.h"
#include <stdio.h>
#include <string.h>

static uint8_t memory[1024];

static bool read_memory(void *ctx, uint32_t address, uint8_t *out, size_t n) {
    (void)ctx;
    if (address > sizeof(memory) || n > sizeof(memory) - address)
        return false;
    memcpy(out, memory + address, n);
    return true;
}

static bool write_memory(void *ctx, uint32_t address, const uint8_t *in, size_t n) {
    (void)ctx;
    if (address > sizeof(memory) || n > sizeof(memory) - address)
        return false;
    memcpy(memory + address, in, n);
    return true;
}

int main(void) {
    i8087 fpu;
    i8087_bus bus = {NULL, read_memory, write_memory};

    struct instruction {
        uint8_t opcode, modrm;
        uint32_t address;
    } code[] = {
        {0xdb, 0x06, 0}, /* FILD dword [0]: 2 */
        {0xdb, 0x06, 4}, /* FILD dword [4]: 3 */
        {0xde, 0xc1, 0}, /* FADDP ST(1),ST(0): 5 */
        {0xdb, 0x06, 8}, /* FILD dword [8]: 4 */
        {0xde, 0xc9, 0}, /* FMULP ST(1),ST(0): 20 */
        {0xdb, 0x1e, 12} /* FISTP dword [12] */
    };

    size_t i;
    memory[0] = 2;
    memory[4] = 3;
    memory[8] = 4;
    i8087_init(&fpu);
    for (i = 0; i < sizeof(code) / sizeof(code[0]); ++i) {
        i8087_result r = i8087_execute(&fpu, &bus, code[i].opcode, code[i].modrm,
                                       (uint32_t)(0x100 + 4 * i), code[i].address);
        if (r != I8087_OK) {
            fprintf(stderr, "Instruction %zu: %s\n", i, i8087_result_string(r));
            return 1;
        }
    }
    printf("(2 + 3) * 4 = %u\n", (unsigned)memory[12]);
    printf("Control=%04x Status=%04x Tags=%04x\n", (unsigned)fpu.control, (unsigned)fpu.status,
           (unsigned)fpu.tags);
    return memory[12] == 20 ? 0 : 1;
}
