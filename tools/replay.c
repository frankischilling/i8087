/* SPDX-License-Identifier: MIT
 * Raw register-state replay tool. File formats and provenance rules are in
 * docs/HARDWARE_VALIDATION.md. Does not use host floating-point instructions. */
#include "i8087.h"
#include "trace_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct image_bus {
    uint8_t bytes[94];
} image_bus;

static bool rd(void *ctx, uint32_t address, uint8_t *dst, size_t n) {
    image_bus *m = ctx;
    if (address != 0 || n > sizeof(m->bytes))
        return false;
    memcpy(dst, m->bytes, n);
    return true;
}

static bool wr(void *ctx, uint32_t address, const uint8_t *src, size_t n) {
    image_bus *m = ctx;
    if (address != 0 || n > sizeof(m->bytes))
        return false;
    memcpy(m->bytes, src, n);
    return true;
}

static bool complete_payload(FILE *f, uint32_t count, unsigned size) {
    long length;
    if (fseek(f, 0, SEEK_END))
        return false;
    length = ftell(f);
    if (length < 0 || (uint64_t)length != TRACE_HEADER_SIZE + (uint64_t)count * size)
        return false;
    return fseek(f, (long)TRACE_HEADER_SIZE, SEEK_SET) == 0;
}

static bool header(FILE *f, uint8_t *h, const char *magic, unsigned size) {
    unsigned i;
    if (fread(h, 1, TRACE_HEADER_SIZE, f) != TRACE_HEADER_SIZE || memcmp(h, magic, 8) ||
        trace_u16(h + 12) != size || trace_u16(h + 14) || !trace_u32(h + 8) ||
        trace_u32(h + 8) > TRACE_MAX_RECORDS || trace_u32(h + 16) > 0xfffff)
        return false;
    for (i = 20; i < TRACE_HEADER_SIZE; ++i)
        if (h[i])
            return false;
    return complete_payload(f, trace_u32(h + 8), size);
}

int main(int argc, char **argv) {
    FILE *in = NULL, *out = NULL, *ref = NULL;
    uint8_t h[TRACE_HEADER_SIZE], record[TRACE_INPUT_SIZE], result[TRACE_OUTPUT_SIZE];
    uint32_t count, i, ip, blocked = 0, pending = 0;
    const char *reference = NULL;
    bool approximate = false;
    int argument, exitcode = 1;
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s input.bin output.bin [--reference hardware.bin] [--approximate]\n",
                argv[0]);
        return 2;
    }
    for (argument = 3; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "--approximate"))
            approximate = true;
        else if (!strcmp(argv[argument], "--reference") && argument + 1 < argc)
            reference = argv[++argument];
        else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[argument]);
            return 2;
        }
    }
    if (!strcmp(argv[1], argv[2]) || (reference && !strcmp(reference, argv[2]))) {
        fprintf(stderr, "input/reference and output paths must differ\n");
        return 2;
    }
    in = fopen(argv[1], "rb");
    if (!in || !header(in, h, "I87V0001", TRACE_INPUT_SIZE)) {
        fprintf(stderr, "invalid or unreadable input header\n");
        goto done;
    }
    count = trace_u32(h + 8);
    ip = trace_u32(h + 16);
    if (reference) {
        uint8_t rh[TRACE_HEADER_SIZE];
        ref = fopen(reference, "rb");
        if (!ref || !header(ref, rh, "I87H0001", TRACE_OUTPUT_SIZE) || trace_u32(rh + 8) != count) {
            fprintf(stderr, "invalid hardware reference header/count\n");
            goto done;
        }
        ip = trace_u32(rh + 16);
    }
    /* C11 exclusive creation also rejects alternate paths and hard-link aliases. */
    out = fopen(argv[2], "wbx");
    if (!out) {
        perror("new output file");
        goto done;
    }
    memcpy(h, "I87M0001", 8);
    trace_put(h + 12, TRACE_OUTPUT_SIZE, 2);
    trace_put(h + 16, ip, 4);
    if (fwrite(h, 1, sizeof(h), out) != sizeof(h))
        goto io_error;
    for (i = 0; i < count; ++i) {
        i8087 f, save;
        image_bus m;
        i8087_bus bus = {&m, rd, wr};
        i8087_result status;
        if (fread(record, 1, sizeof(record), in) != sizeof(record))
            goto io_error;
        if (!trace_register_opcode(record[0], record[1])) {
            fprintf(stderr, "record %lu is not a supported register encoding\n", (unsigned long)i);
            goto done;
        }
        memcpy(m.bytes, record + 2, 94);
        i8087_init(&f);
        if (approximate)
            i8087_set_policy(&f, I8087_ALLOW_APPROXIMATION);
        if (i8087_execute(&f, &bus, 0xdd, 0x26, 0, 0) != I8087_OK)
            goto io_error;
        status = i8087_execute(&f, &bus, record[0], record[1], ip, 0);
        if (status == I8087_UNVERIFIED)
            ++blocked;
        if (status == I8087_PENDING_INTERRUPT)
            ++pending;
        memset(result, 0, sizeof(result));
        trace_put(result, (uint32_t)status, 2);
        trace_put(result + 2, f.last_gap, 4);
        save = f;
        if (i8087_execute(&save, &bus, 0xdd, 0x36, 0, 0) != I8087_OK)
            goto io_error;
        memcpy(result + 6, m.bytes, 94);
        if (fwrite(result, 1, sizeof(result), out) != sizeof(result))
            goto io_error;
    }
    if (fgetc(in) != EOF || ferror(in)) {
        fprintf(stderr, "trailing data or input error\n");
        goto done;
    }
    if (fflush(out))
        goto io_error;
    printf(
        "MODEL replay: %lu records; %lu guarded; %lu pending interrupts; NOT hardware evidence\n",
        (unsigned long)count, (unsigned long)blocked, (unsigned long)pending);
    exitcode = 0;
    goto done;
io_error:
    fprintf(stderr, "truncated input, invalid state, or I/O error\n");
done:
    if (in && fclose(in)) {
        perror("close input");
        exitcode = 1;
    }
    if (ref && fclose(ref)) {
        perror("close reference");
        exitcode = 1;
    }
    if (out && fclose(out)) {
        perror("close output");
        exitcode = 1;
    }
    return exitcode;
}
