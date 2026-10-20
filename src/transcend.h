/* SPDX-License-Identifier: MIT */
#ifndef I8087_TRANSCEND_H
#define I8087_TRANSCEND_H
#include "soft80.h"
/* Deterministic functional approximations, NOT original 8087 microcode. */
bool sf_transcend(uint8_t modrm, i8087_f80 x, i8087_f80 y,
                  sf_result *result, i8087_f80 *pushed);
#endif
