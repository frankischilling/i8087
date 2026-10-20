# FNOP and the reserved D9 D1 through D9 D7 encodings

Research date: 2026-10-20. This note concerns original Intel 8087 decoding.
It supplies no physical execution trace and does not establish an undocumented
alias for any chip stepping.

## Finding

The inspected evidence does not yet prove whether `D9 D1` through `D9 D7`
execute as FNOP on an original 8087. Keep these encodings in the unresolved
opcode audit. A shared microcode entry makes the alias hypothesis plausible,
but its selection for the seven reserved encodings remains unverified.

Intel documents only `D9 D0` as FNOP. Table A-2 in the 1981 manual, printed
S-112, gives these entries; the scan was visually checked at PDF page 795.
[Intel decoding guide](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=795).

| Encoding | Intel entry |
| --- | --- |
| `D9 D0` | FNOP |
| `D9 D1` | Reserved |
| `D9 D2` and `D9 D3` | Reserved, second byte `1101 001-` |
| `D9 D4` through `D9 D7` | Reserved, second byte `1101 01--` |

Reserved status establishes the documentation boundary. It does not establish
that the physical chip rejects the encoding, raises an exception, hangs, or
performs a no-op. A strict emulator's refusal to execute an unresolved encoding
must not be presented as a measured hardware response.

## What the die-derived decoder evidence establishes

Shirriff's first-hand analysis identifies 22 microcode entry selections.
Entry 15 selects decimal address `#0217` for FNOP, FST and non-BCD FSTP. His
register-opcode table also states that FNOP has a single opcode, leaving the
other positions unused. That statement is consistent with Intel's documented
table and does not explicitly characterize the reserved positions electrically.
[Instruction decoding, notes 8 and 9](https://www.righto.com/2026/02/8087-instruction-decoding.html).

The article describes a preprocessing signal for `001 111xxxxx`, where the
R/M bits select miscellaneous operations. `D9 D0` through `D9 D7` fall outside
that pattern. This supports investigating whether their bottom bits are
irrelevant. The article also describes feedback between PLA outputs and inputs.
It does not provide a complete Boolean term map assigning each reserved input
to an entry or establish that no separate logic distinguishes these cases.
The published partial PLA photograph and block labels were insufficient for
an independent end-to-end reconstruction of this selection.
[Decoder preprocessing and die images](https://www.righto.com/2026/02/8087-instruction-decoding.html).

## Conditional microcode result

The following path is useful evidence **if an encoding reaches `#0217`**.
Addresses are decimal. The inspected recovered listing is pinned to commit
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

| Address | Operation relevant to this question |
| --- | --- |
| `#0217` | Copy ST(0) to internal `tmpA`. |
| `#0218` | Jump to `#0224` when condition `0x02` is true. |
| `#0224` | Jump to `#0227` when condition `0x06` is false. |
| `#0227` | Read next instruction. |

This route skips the tag check at `#0219`, the register write at `#0223`,
the pop at `#0225` and `#0226`, and the invalid-exception path at `#0228`.
It contains no precision adjustment. [Recovered microcode](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L218).

The newer die-derived condition table defines `0x02` as `0XX 11XXXXXX` and
`0x06` as `110 xxxxxxxx` or `111 xx0xxxxx`. All eight candidates have the
11-bit pattern `001 11010rrr`. Consequently `0x02` is true and `0x06` is
false for every candidate, regardless of `rrr`. This is a conditional inference
from the published predicates, whose author explicitly warns that the table
is still under investigation. [Condition decoding, note 7](https://www.righto.com/2025/12/8087-microcode-conditions.html).

The recovered `main.cpp` reads `8087mc.txt` and disassembles its 16-bit
microinstructions. It is not an executable truth table for the external opcode
PLA. Its shared-entry annotation cannot fill the entry-selection gap.
[Disassembler source](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/main.cpp#L356).

## Evidence needed to resolve the aliases

Either of these would materially resolve the question:

- A reproducible die transcription of the input preprocessing, relevant PLA
  product terms, output selection and any BIU qualification, showing the selected
  entry for each of the eight raw opcodes.
- Attributable original-8087 captures covering all eight encodings, with chip
  markings, host configuration, raw input state, raw saved output and completion
  information. Later x87 results cannot identify the original decoder behavior.

A first capture campaign should use all eight opcodes, all eight TOP values,
and these three states, giving 192 cases:

1. A full stack of distinct normal values, with ST(0) equal to +1 and ST(1)
   through ST(7) equal to +2 through +8. This exposes an accidental store to ST(i).
2. The same physical payloads with only the current ST(0) tag changed to EMPTY.
   This exposes an operand or stack check that the conditional path skips.
3. A full stack with ST(0) set to raw sign/exponent `4000` and significand
   `4000000000000000`, tagged VALID. This exposes a raw write or normalization
   that ordinary +1 would hide.

Start with control word `03bf`, clean exception state and a known condition-code
pattern. Record all raw registers, tags, TOP, control/status words and saved
instruction information. Compare each candidate with `D9 D0` under the same
setup, accounting explicitly for instruction-address bookkeeping. Capture
completion or a bounded timeout instead of assuming every reserved encoding
returns. Precision/rounding settings, preexisting sticky flags and unmasked
pending exceptions are separate follow-up cases. Bus and BUSY traces are needed
before assigning the aliases the documented FNOP timing.

The bounded search inspected Intel's encoding tables, Shirriff's decoder
analysis and linked images, the recovered microcode files and disassembler,
and exact-opcode searches for `D9D1`, `D9 D7` and FNOP aliases. No attributable
original-8087 execution capture for these seven inputs was found in that search.
This is an evidence gap, not a claim that such captures do not exist.
