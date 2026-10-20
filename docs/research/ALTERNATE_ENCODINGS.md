# Intel-documented alternate 8087 register encodings

Research date: 2026-10-20. Intel's original decoding guide explicitly assigns
operations to nine encoding groups that its language translators do not emit.
These are documented 8087 forms. This note does not supply physical traces,
identify a silicon stepping or validate exact cycle counts.

## Source and page numbering

The source is Table A-2 in the 1981 iAPX 86,88 User's Manual. All three pages
and the concluding footnote were visually inspected in the PDF scan.

| Printed page | PDF ordinal, starting at 1 | PDF index, starting at 0 |
| --- | --- | --- |
| [S-112](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=795) | 795 | 794 |
| [S-113](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=796) | 796 | 795 |
| [S-114](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=797) | 797 | 796 |

The footnote on S-114 says the translators omit the marked forms and the 8087
executes them as listed below. It gives an explicit operation for each marker.

## Exact groups

`REG` denotes the low three bits of the second byte, giving `i = 0` through 7.
Each inclusive hexadecimal range contains eight encodings. Footnote numbers
refer to the marked entries and explanations in Intel Table A-2.

| Footnote | Bytes | Second-byte pattern | Operation | Entry page |
| --- | --- | --- | --- | --- |
| 1 | `D9 D8` through `D9 DF` | `1101 1REG` | FSTP ST(i) | S-112 |
| 2 | `DC D0` through `DC D7` | `1101 0REG` | FCOM ST(i) | S-113 |
| 3 | `DC D8` through `DC DF` | `1101 1REG` | FCOMP ST(i) | S-113 |
| 4 | `DD C8` through `DD CF` | `1100 1REG` | FXCH ST(i) | S-113 |
| 5 | `DE D0` through `DE D7` | `1101 0---` | FCOMP ST(i) | S-114 |
| 6 | `DF C0` through `DF C7` | `1100 0REG` | FFREE ST(i), then pop | S-114 |
| 7 | `DF C8` through `DF CF` | `1100 1REG` | FXCH ST(i) | S-114 |
| 8 | `DF D0` through `DF D7` | `1101 0REG` | FSTP ST(i) | S-114 |
| 9 | `DF D8` through `DF DF` | `1101 1REG` | FSTP ST(i) | S-114 |

For group 5, the table prints dashes, while its footnote specifies ST(i).
Preserve that source distinction when transcribing the table. The groups total
72 encodings. [Intel operation footnotes](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=797).

## FFREE followed by a pop

The recovered microcode resolves what footnote 6's pop does to the old top.
Addresses are decimal; the inspected snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

| Address | Action for `DF C0` through `DF C7` |
| --- | --- |
| `#0065`, `#0066` | Enter the FLD/FFREE routine and select its FFREE path. |
| `#0074` | Mark the selected ST(i) EMPTY. |
| `#0075` | Continue because the pop condition is true. |
| `#0076` | Also mark the old ST(0) EMPTY. |
| `#0077` | Increment TOP. |
| `#0078` | Finish the instruction. |

These are two explicit tag writes before the TOP change. For `i = 0`, both
writes select the same physical register; for other i values, both registers
become EMPTY. The listed path changes tags and TOP without replacing the raw
register payloads. [Recovered FFREE path](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L66).

Condition `0x01` accepts `1XX 11XXXXXX`; pop condition `0x06` accepts
`110 xxxxxxxx` or `111 xx0xxxxx`. The `DF C0` group matches both. Ordinary
`DD C0` FFREE matches the first condition and skips the pop. The published
predicate table is provisional. [Die-derived condition table, note 7](https://www.righto.com/2025/12/8087-microcode-conditions.html).

Consequently footnote 6 must not be implemented as FFREE followed only by
FINCSTP. FINCSTP changes TOP without emptying the old top; the decoded path
above explicitly empties it. This is a microcode-derived ordering result,
still awaiting physical confirmation. [FINCSTP words at `#1466` and `#1467`](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L1467).

## Implementation and capture scope

The decoder and hardware collector's permitted register-opcode list include
these exact groups on the strength of the original manual. Collector acceptance
means an input is within this documented campaign scope. It does not mean an
output has already been collected or verified on physical hardware.

Use the original bytes in a capture fixture so the assembler cannot silently
choose its usual encoding. Preserve them in saved instruction information.
FSTP cases must exercise the destination write and pop; FCOMP cases must check
comparison flags and one pop; FCOM and FXCH must not gain a pop. For footnote 6,
check both EMPTY tag writes and the subsequent TOP increment.

The regression suite covers all 72 forms at every TOP, with distinct normal
payloads, empty source/destination cases, signed zeros, inconsistent tags, raw
unnormals, NaN payloads and sticky exceptions. Literal expectations check the
normal stack/tag/flag results and the two FFREE tag writes. Exceptional cases
also compare against the existing operation semantics. See
[the test scope](../TESTING.md#original-alternate-encodings).

`--suite encodings` emits 72 masked inputs, one per encoding at TOP zero, for
the unverified hardware collector. Generating and replaying them establishes
tooling consistency. Future original captures should repeat the campaign at
every TOP and preserve distinct evidence labels for document-derived
expectations and silicon observations.

This source does not authorize other reserved encodings. In particular,
`D9 D1` through `D9 D7` are explicitly reserved on S-112 and are outside these
nine groups. Keep that FNOP-alias question open until original decoder or
hardware evidence resolves it. No later x87 alias table is needed for the
forms listed here.
