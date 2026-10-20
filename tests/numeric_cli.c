/* SPDX-License-Identifier: MIT
 * Line-oriented integer-hex bridge for the independent Python Fraction oracle.
 */
#include "soft80.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char line[256], op[16];
    unsigned p, rc, ae, be;
    uint64_t as, bs;
    while (fgets(line, sizeof(line), stdin)) {
        i8087_f80 a, b;
        sf_result r;
        if (sscanf(line, "%15s %u %u %x %" SCNx64 " %x %" SCNx64, op, &p, &rc, &ae, &as, &be,
                   &bs) != 7)
            return 2;
        if ((p != 24 && p != 53 && p != 64) || rc > 3 || ae > 0xffff || be > 0xffff)
            return 2;
        a.sign_exponent = (uint16_t)ae;
        a.significand = as;
        b.sign_exponent = (uint16_t)be;
        b.significand = bs;
        if (!strcmp(op, "add"))
            r = sf_add(a, b, p, rc);
        else if (!strcmp(op, "sub")) {
            b.sign_exponent ^= 0x8000;
            r = sf_add(a, b, p, rc);
        } else if (!strcmp(op, "mul"))
            r = sf_multiply(a, b, p, rc);
        else if (!strcmp(op, "div"))
            r = sf_divide(a, b, p, rc);
        else if (!strcmp(op, "sqrt"))
            r = sf_sqrt(a, p, rc);
        else if (!strcmp(op, "round"))
            r = sf_round_integer(a, rc);
        else if (!strcmp(op, "store32") || !strcmp(op, "store64") || !strcmp(op, "int16") ||
                 !strcmp(op, "int32") || !strcmp(op, "int64")) {
            uint16_t flags = 0;
            bool up = false;
            uint64_t bits;
            unsigned width = !strcmp(op, "store32") || !strcmp(op, "int32") ? 32
                             : !strcmp(op, "int16")                         ? 16
                                                                            : 64;
            if (op[0] == 's')
                bits = sf_store_ieee(a, width, rc, &flags, &up);
            else
                bits = sf_to_integer(a, width, rc, &flags, &up);
            printf("%04x %016" PRIx64 " %04x %u\n", 0, bits, flags, (unsigned)up);
            continue;
        } else
            return 2;
        printf("%04x %016" PRIx64 " %04x %u\n", r.value.sign_exponent, r.value.significand, r.flags,
               (unsigned)r.rounded_up);
    }
    return ferror(stdin) ? 3 : 0;
}
