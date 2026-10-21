# Primary references

The implementation was written against original Intel documentation. No source
from another emulator or floating-point library is included in this package.
The manuals are not redistributed.

The [F2XM1 signed-zero investigation](research/F2XM1_ZERO.md) combines Intel's
signed-zero table with the decoded ZERO-tag early return. It supports that
bounded path without using the general transcendental fallback.

The [reserved FNOP encoding investigation](research/FNOP_DECODING.md) records
why shared microcode does not establish an alias without decoder selection
evidence. The reserved encodings remain part of the open opcode audit.

The [logarithm special-case investigation](research/LOGARITHM_SPECIAL_CASES.md)
records the FYL2XP1 ZERO-tag branch and unresolved sign operations. These
logarithm paths remain guarded; mathematical identities do not establish their
original raw results or exception behavior.

The current [hardware-evidence search](research/HARDWARE_EVIDENCE.md) records
revision-identification limits, the initial IBM 5150-style target and capture
leads. A source edition or speed grade does not identify a silicon stepping.
The [constant-load investigation](research/CONSTANT_LOADS.md) separates the
decoded fixed-transfer path from the still-unconfirmed payload bits.

The [bus-timing investigation](research/8088_BUS_TIMING.md) records original
pin setup/hold, queue reports, reset and operand-read evidence. A pinned AMD
8088 ESC corpus supplies host trace examples with explicit provenance; it is
not a physical Intel 8087 execution reference. Internal queue ordering,
opcode-latch delays and complete coprocessor timing remain unresolved.

## Intel 8087 datasheet, order 205835-007

Intel, *8087 Numeric Data Processor*, archived PDF:

https://datasheets.chipdb.org/Intel/x86/808x/datashts/8087/205835-007.pdf

Relevant printed pages / PDF page indices:

- 3-94 / index 4: numeric data formats.
- 3-97 / index 7: execution-unit and bus-interface overview.
- 3-98 / index 8: status and physical tag words, Figures 6 and 7.
- 3-99 / index 9: condition-code tables and 14-byte environment pointer layout.
- 3-100 / index 10: control word, rounding, precision and infinity control.
- 3-101 / index 11: exception behavior.

The bitmap figures were inspected, not inferred exclusively from OCR. Where
the status-word prose disagrees with its diagram's TOP bit positions, this
implementation follows the diagram (TOP at bits 11..13).

## Intel iAPX 86,88 User's Manual (1981), 8087 supplement

Original manual archived at:

https://archive.org/details/bitsavers_inteldataBrsManual_57011881

Searchable archive text consulted alongside the datasheet:

https://archive.org/stream/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual_djvu.txt

Relevant parts of the 8087 supplement:

- Initialization and interrupt handling: initialization does not erase data
  registers or exception pointers; separate global and per-exception masks.
- Processor control, printed S-40/S-41: FSTENV/FNSTENV masks individual
  exceptions but does not change the global interrupt mask; FSAVE/FRSTOR
  and FSTENV/FLDENV layouts.
- Instruction descriptions: original F2XM1, FPTAN, FPATAN, FYL2X and FYL2XP1
  operand domains and result-stack behavior.
- Advanced/exception discussions: unnormal values, propagation of lost
  significance, projective infinity and original NaN behavior.
- Instruction coding tables: original documented ESC encodings, as distinct
  from instructions added by later x87 processors.

The archive text is OCR and is not itself a golden executable specification.
The code's unverified behavior and missing hardware validation are recorded in
ACCURACY.md, not inferred away from ambiguous text.

The [directed-overflow investigation](research/DIRECTED_OVERFLOW.md) uses
visually inspected S-75, S-78, S-80 and S-81 pages. S-75 explicitly specifies
the original PE/OE policy. The finite-bound interpretation also uses the
recovered precision-adjustment and saturation ordering; a conflicting printed
significand footnote is retained in the research record.

## Original instruction and exception details

The [unnormal-plus-zero investigation](research/UNNORMAL_ZERO_ARITHMETIC.md)
combines Intel's normalization rule with the decoded alignment and precision
paths. It supports a bounded PC64 result that retains the unnormal exponent
and significand; it does not establish general unnormal arithmetic.

Table A-2, printed S-112 through S-114, explicitly assigns operations to nine
register encoding groups that Intel's translators do not emit. The decoder
accepts these 72 forms. [The encoding investigation](research/ALTERNATE_ENCODINGS.md)
records the exact bytes, original footnotes and recovered FFREE-plus-pop tag
sequence. Reserved encodings outside that list remain unresolved.

The 1981 manual's FXTRACT description explicitly discusses signed-zero results
and field extraction. The FPREM condition-code shift rule is also visible in
Table 4b of the datasheet, printed 3-99 (PDF index 9). Exception discussions
separate before-operation and after-operation exceptions. The supplement's
S-67/S-68 range discussion describes register exponent adjustment by 24,576,
while Table S-32 lists operand/operation-specific exception cases and Table S-33
lists masked responses. These sources informed the regression expectations;
the tests are not captures from those sources or from original hardware.

One unresolved detail is masked overflow with round-toward-zero: the searchable
1981 manual says infinity for nearest or CHOP, while the die-level description
requires careful interpretation of the hardware round/chop decision, which is
not simply the control-word RC field. The implementation keeps that particular
fallback guarded instead of resolving the discrepancy by assumption.

## Ken Shirriff / Opcode Collective die-level research

These are first-hand reverse-engineering reports, not a later x87 reference.
Their interpretations are valuable evidence but are not treated as a substitute
for captured original-processor outputs. Accessed 2026-10-20.

- *Microcode inside the Intel 8087 floating-point chip: register exchange*
  (May 2026): register/tag transfers, internal microinstruction structure and
  per-instruction exception state.
  https://www.righto.com/2026/05/microcode-inside-intel-8087-floating.html
- *Microcode in Intel's 8087 floating-point chip: the scale instruction*
  (September 2026): FSCALE zero-tag early exits (#0748 onward), exponent-only
  scaling, special-operand handling and NONNORMAL_RESULT exponent adjustment.
  https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html
- *Reverse-engineering the vintage Intel 8087's tangent algorithm: more than CORDIC*
  (September 2026): FPTAN's tiny/zero bypass, sixteen CORDIC steps, rational
  remainder approximation and separately returned coordinates. Only the
  zero/tiny bypass has been implemented as an unguarded path here. The general
  software fallback is NOT the described original algorithm.
  https://www.righto.com/2026/09/8087-tangent-cordic.html
- *Extracting ROM constants from the 8087 math coprocessor's die* (May 2020):
  physical constant-ROM analysis. The nontrivial load constants remain guarded
  because their entire loading/rounding path has not been validated here.
  https://www.righto.com/2020/05/extracting-rom-constants-from-8087-math.html

No third-party emulator source, microcode ROM image, manual scan or article is
redistributed in this package. The implementation uses independently written C
and tests based on the documented behavior. There is no claim that a particular
silicon revision has been reconstructed or validated from those references.
