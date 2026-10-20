# Masked directed overflow at reduced precision

Research date: 2026-10-20. This investigation used original Intel documentation
and recovered-die research. It produced no original-hardware captures.

## Finding

For a **normal register result** whose directed overflow saturates to a finite
value, the decoded microcode supports a full 64-bit all-ones significand at
PC=24, PC=53 and PC=64. Precision adjustment precedes construction of the
saturation value. The finite response is then constructed at full width, with
no intervening precision-adjustment call before the register write.

This is a strong implementation hypothesis based on the decoded path, not a
measurement of a named 8087 specimen. It should not be generalized to unnormal
results, memory stores or the unresolved CHOP case. The instruction layer follows
Intel's explicit directed-overflow flag rule; that policy still needs hardware
validation alongside the raw result.

## Intel specification and the printed discrepancy

The 1981 manual's S-75 prose and S-81 Table S-33 give these normal-result
directions: positive/down and negative/up deliver a finite bound; positive/up
and negative/down deliver signed infinity. S-75 says the finite bound belongs
to the destination format. It also states that directed masked overflow does
not set the overflow flag and does raise the precision exception. [Intel S-75](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=758),
[S-81](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=764).

S-81 footnote 1 visibly prints a significand ending in zero, `(1)^11...10B`.
That conflicts with Table S-31's temporary-real normal endpoints at S-78,
which show an all-ones significand with exponent `11...10`. Both scans were
visually checked. A printing error is the likely explanation, but that remains
an inference; retain the discrepancy in the source record. [Intel S-78](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=761).

S-80 separately specifies infinity for masked nearest **or CHOP** overflow,
for arithmetic and short/long real stores. Do not silently replace this with
later x87 expectations. [Intel S-80](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=763).

## Microcode ordering

All addresses below are decimal. The recovered listing snapshot is granite
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

FMUL calls J11 at `#0384`, writes the rounded fraction at `#0385`, computes the
exponent, and returns at `#0395`. Common arithmetic code subsequently calls
J16 at `#0010`; the final destination write is at `#0012` or `#0014`.
[FMUL listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L385),
[common arithmetic listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L10).

Shirriff identifies J11 as ADJUST_PRECISION and J16 as NONNORMAL_RESULT. His
addition analysis independently places precision adjustment at `#0290` before
the return to the common overflow handling. [Addition analysis](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

Within the finite saturation path, `#0350` selects 1, `#0351`/`#0352` reduce
the infinity exponent to `0x7ffe`, and `#0353`/`#0354` form the significand by
subtracting 1 from zero. `#0355` marks the result valid. Normal results return
at `#0356`; the old fraction is restored at `#0357` for unnormal results.
No J11 call follows this construction. The same interpretation explicitly
describes the result as the largest float. [NONNORMAL_RESULT analysis](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

The PC independence conclusion follows from that ordering and the full-width
fraction write. It assumes no hidden precision truncation on that write; the
decoded instruction stream alone is not a pin-level execution measurement.

The old generated listing labels `#0346` as overflow, whereas Shirriff's newer
decoding labels it precision. Similarly, the phrase `round chop` at `#0349`
names an internal rounding decision; it must not be equated directly with
RC=`11`. Those labels cannot resolve the CHOP disagreement or the OE behavior
by themselves. [Recovered saturation listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L345).

## Register and memory destinations

The instruction layer leaves a normal finite saturation result at exponent `0x7ffe` with
significand `0xffffffffffffffff`, retaining the result sign. Do not round that
constructed bound again to 24 or 53 bits. The canonical `quantize` backend
gives `0xffffff0000000000` at PC24 and `0xfffffffffffff800` at PC53. The
instruction layer replaces those bounds after the backend calculation.
That difference is observable in the 80-bit register image.

FST to short or long real has a separate conversion/packing path, J8, beginning
at `#0439`; its overflow response is around `#0484` through `#0497`, followed
by width-dependent packing. The register inference does not justify writing
an 80-bit saturation image to either memory format. [Store listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L440).

The destination-format finite bounds are binary32 `0x7f7fffff`/`0xff7fffff`
and binary64 `0x7fefffffffffffff`/`0xffefffffffffffff`, for positive/negative
respectively. Test store width separately from arithmetic PC; storing a result
can hide the lower register bits that distinguish the two hypotheses.

## Discriminating register cases

Use a fresh state with TOP=0, no pending exceptions, ST(0) and ST(1) valid,
and all remaining registers empty. With physical tags in that order the tag
word is `0xfff0`. Execute `D8 C9` (`FMUL ST(0), ST(1)`).

| Operand | Sign/exponent | Significand |
| --- | --- | --- |
| Positive ST(0), largest normal temporary real | `0x7ffe` | `0xffffffffffffffff` |
| Negative ST(0), alternate campaign | `0xfffe` | `0xffffffffffffffff` |
| ST(1), +2 | `0x4000` | `0x8000000000000000` |

Use `CW = 0x10bf | (PC << 8) | (RC << 10)`: all six exception masks set,
global interrupt mask set, affine infinity selected, reserved bit 6 clear.

| ST(0) sign / RC | PC24 (`00`) CW | PC53 (`10`) CW | PC64 (`11`) CW | Predicted finite sign/exponent |
| --- | --- | --- | --- | --- |
| Positive, down (`01`) | `0x14bf` | `0x16bf` | `0x17bf` | `0x7ffe` |
| Negative, up (`10`) | `0x18bf` | `0x1abf` | `0x1bbf` | `0xfffe` |

The decoded-path hypothesis predicts significand `0xffffffffffffffff` in all
six outputs. PC24 and PC53 discriminate against the current saturation value;
PC64 is a control. These are proposed expected records, not hardware traces.
Also run positive/up and negative/down as infinity controls, and nearest and
CHOP as separate investigations.

Capture the raw 94-byte state so lower significand bits survive. Compare tags
and defined exception bits separately from the significand. Intel's S-75 flag
rule predicts PE set without newly setting OE for directed masked overflow.
The instruction layer applies that rule while preserving an existing sticky OE.
The `#0319` instruction delegates overflow/underflow
flags to hardware, whose full directed-rounding behavior has not been decoded
here. Keep the source-based status expectation distinct from a verified silicon
claim, and test the status policy separately from the significand correction.

A complete follow-up should repeat the six register cases on identified
original specimens, add corresponding FST32/FST64 cases, and record which
specification and die interpretation each expectation follows. The current
register-only hardware collector cannot execute the memory-store fixtures.
