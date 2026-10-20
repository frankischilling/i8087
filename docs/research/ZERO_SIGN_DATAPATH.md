# Unresolved sign-control words in original 8087 zero paths

Research date: 2026-10-20. Scope: raw microinstructions `6580` and `6088`,
especially multiplication and the J13 completion path shared by FYL2X and
FYL2XP1. This note narrows the missing decode. It does not establish exact
FYL2XP1 signed-zero behavior or supply a physical execution trace.

## Finding

Keep the strict logarithm guard. Published evidence supports a specific sign
assignment hypothesis, but does not yet prove its destination routing or its
complete tag, condition-code and exception side effects. Nearby decoded words
are useful evidence; treating an incomplete field description as an exhaustive
hardware specification would overstate it.

## Inspected public source state

The latest commit returned for granite's `tools/8087mc` path was
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`, dated 2026-08-12. Its notes,
disassembler and generated listing were inspected, together with Shirriff's
public fork branches. This bounded check found no newer published decode of
the two words. [Path history](https://github.com/a-mcego/granite/commits/main/tools/8087mc),
[Shirriff fork](https://github.com/shirriff/granite).

`getBCopOp` recognizes several sign operations but leaves both words unnamed.
Its displayed `op` field differs from the six-bit function argument passed to
that helper. Keep the underlying bit fields when comparing words.
[Disassembler helper and caller](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/main.cpp#L108).

| Word | ABC | DEFGH | IJ | MNO | P |
| --- | --- | --- | --- | --- | --- |
| `6580` | `011` | `00101` | `10` | `000` | `0` |
| `6584` | `011` | `00101` | `10` | `010` | `0` |
| `6088` | `011` | `00000` | `10` | `100` | `0` |
| `6084` | `011` | `00000` | `10` | `010` | `0` |
| `6280` | `011` | `00010` | `10` | `000` | `0` |

The letters follow the recovered notes, with A the most significant bit.
[Microinstruction notation](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L7).

## Stronger anchors from first-hand die reports

Shirriff's October addition/subtraction report decodes `#0023`, whose raw word
is `6584`, as condition `3b` into the sign latch. The same report decodes
`#0062` (`6084`) as the tmpA sign into that latch and `#0030` (`6280`) as
the latch into tmpA's sign. His condition table defines `3b` as a mismatch
between the temporary signs. These give a concrete source-selector lead.
[Addition/subtraction microcode](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html),
[condition table, note 7](https://www.righto.com/2025/12/8087-microcode-conditions.html).

Comparing the words suggests this hypothesis:

- `6580`: write `sign(tmpA) XOR sign(tmpB)` into tmpA's sign.
- `6088`: copy tmpA's sign into tmpB's sign.

This inference uses related decoded bit patterns. It is not a transistor-traced
or explicitly published decode of the two requested words. In particular,
changing MNO from `010` to `000` or `100` must be shown to select only the
intended destination. The existing examples do not prove that all other write
enables remain inactive.

The notes list some tag and condition-code controls, but do not supply an
exhaustive control truth table for these words. The condition table is itself
marked provisional. Neither absence of an annotation nor similarity to a
known sign transfer proves unchanged tags or C0, C1, C2 and C3.
[Partial BC-operation notes](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L156).

## Where the missing decode matters

| Word | Decimal addresses in the recovered listing |
| --- | --- |
| `6580` | `#0359` multiplication, `#0410` division, `#1449` J13 |
| `6088` | `#0161` conversion, `#0361` multiplication, `#0606` packed-decimal store, `#1451` J13 |

These locations provide additional control-flow comparisons. They do not by
themselves establish a sign rule. [Recovered listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L360).

In J13, `#1449` follows the pop and load of the former Y into tmpB. For zero Y,
`#1451` precedes writing tmpB. For nonzero Y, `#1454` tests tmpA's ZERO tag
before selecting the direct result write. The hypothesis predicts an XOR sign
for canonical zero X and ordinary signed Y, including zero Y. A tag side
effect could change which route runs, while a condition-code side effect could
be observable even if the raw zero is correct. See the separately scoped
[logarithm path investigation](LOGARITHM_SPECIAL_CASES.md).

The relevant Intel processor patent describes the architecture but explicitly
omits the microcode program. It does not resolve these two control words.
The related fraction-bus patent supplies no raw-word mapping found in this
inspection. [Intel US4338675A, logic circuit 68](https://patents.google.com/patent/US4338675A/en),
[Intel US4484259A](https://patents.google.com/patent/US4484259A/en).

## Concrete resolution work

A sufficient circuit decode must identify the selected sign source and every
destination write enable for both words. It must also check temporary-register
renaming, both tag-bit write paths, C0 through C3, and exception controls.
An explicit first-hand decode that reports these effects would close the
current source gap.

For external hardware tests, begin with FYL2XP1 at X equal to each of canonical
+0 and -0, and Y equal to each of +1, -1, +0 and -0. Seed all 16 condition-code
patterns, giving 128 focused cases at TOP 0, PC 64 and nearest rounding.
Record all raw register bytes and tags, TOP and status, with clean exceptions.
The sign hypothesis must be an experimental prediction, not the capture oracle.

If those agree, expand across legal PC/RC settings and TOP, then repeat with
PE unmasked and with preexisting sticky PE/UE. Use the corresponding FMUL
signed-zero cases as a second externally visible route through the same words.
Retain chip markings and host/interrupt configuration with every capture.

Even agreement on ordinary signed zeros would not validate inconsistent tags,
pseudozeros, empty registers, NaNs or infinity inputs. This investigation found
no attributable original-8087 capture that settles the missing side effects.
