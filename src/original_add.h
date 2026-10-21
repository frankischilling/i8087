/* SPDX-License-Identifier: MIT */
#ifndef I8087_ORIGINAL_ADD_H
#define I8087_ORIGINAL_ADD_H

#include "soft80.h"

typedef enum original_add_status {
    ORIGINAL_ADD_COMPLETE,
    ORIGINAL_ADD_C1_UNCHANGED,
    ORIGINAL_ADD_RANGE
} original_add_status;

/* Inputs have documented PC, effective signs, and correctly tagged finite
   nonzero or canonical-zero payloads. The caller handles exceptional inputs.
   C1_UNCHANGED completes without a rounding write; RANGE requires separate
   exceptional completion and does not certify the returned payload. */
original_add_status original_add(i8087_f80 a, i8087_f80 b, unsigned precision, unsigned rc,
                                 sf_result *out);

#endif
