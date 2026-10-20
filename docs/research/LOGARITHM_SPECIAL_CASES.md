# Original 8087 logarithm special cases

Research date: 2026-10-20. Scope: FYL2XP1 with canonical signed zero in ST(0),
and FYL2X with canonical +1 in ST(0). This note contains document and recovered
microcode evidence, with no attributable physical execution capture.

## Finding

Keep both cases guarded until the raw result and exception behavior are
resolved. FYL2XP1 has an early ZERO-tag branch that bypasses the general
logarithm calculation and its explicit precision-exception operation. Its
shared completion routine contains undecoded operations relevant to sign.
FYL2X has no comparable direct entry shortcut for +1 in the inspected listing.
The identity `log2(1) = 0` does not establish its hardware flags or raw result.

## Intel documentation boundary

The 1981 supplement, printed S-38, gives FYL2X a positive finite X range and
finite Y range. It explicitly excludes zero X from FYL2XP1's stated range,
`0 < |X| < 1 - sqrt(2)/2`. Both instructions pop X and replace Y at the new
stack top. Thus FYL2X at +1 is within the stated X range; FYL2XP1 at signed
zero requires evidence beyond that range description.
[Intel S-38, PDF page 721](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=721).

S-37 says transcendental operands are assumed valid and in range, with some
functions accepting zero and others excluding it. S-57 lists precision as the
exception for both logarithms and says operands are not checked. Table S-26
does not supply logarithm signed-zero entries. These pages do not settle the
special cases here. The relevant range and exception pages were visually
checked. [Intel S-37](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=720),
[S-57](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=740),
[S-71](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=754).

## Entry and the FYL2XP1 zero branch

Shirriff identifies entry 19 at decimal address `#1257` for both instructions.
[Die-derived entry table, note 9](https://www.righto.com/2026/02/8087-instruction-decoding.html).
The inspected granite snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

| Address | Relevant operation |
| --- | --- |
| `#1257` | Copy ST(0), including its tag, into `tmpA`. |
| `#1259` | Branch to `#1285` on condition `0x0b`. |
| `#1285` | Select constant index 5. |
| `#1286` | Copy the working exponent to an internal operand register. |
| `#1287` | Jump to J13 when condition `0x20` is true. |

Condition `0x0b` tests opcode bit 3: it selects the FYL2XP1 path for `D9 F9`
and the FYL2X path for `D9 F1`. Condition `0x20` tests the ZERO tag, rather
than independently checking all 80 payload bits. The published condition table
is explicitly provisional. [Condition predicates, note 7](https://www.righto.com/2025/12/8087-microcode-conditions.html).

This early route skips the polynomial/CORDIC work, including `#1371`, whose
raw word is `f004`. The recovered notes identify that word as the precision
exception operation. The generated listing's overflow label for it is stale.
A blanket rule that every logarithm execution raises PE therefore needs more
evidence for this branch. [Generated path listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1258),
[exception decoding notes](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L186).

## J13 and the unresolved result

The raw ROM words and partial decoding give this completion sequence:

| Address | Relevant operation |
| --- | --- |
| `#1446`, `#1447` | Mark the old ST(0) EMPTY and increment TOP once. |
| `#1448` | Read the new ST(0), formerly Y, into `tmpB`. |
| `#1449` | Execute raw word `6580`, still undecoded in the listing. |
| `#1450` | Test the ZERO tag of `tmpB`. |
| `#1451` through `#1453` | For zero Y, execute undecoded `6088`, write `tmpB`, finish. |
| `#1454` | For nonzero Y, test the ZERO tag of `tmpA`. |
| `#1459`, `#1460` | Write `tmpA` at the new top and finish. |

If `tmpA` retains its ZERO tag through `#1449`, the nonzero-Y route skips
the multiplication, precision adjustment and range handling at `#1455` through
`#1458`. The pop precedes those tests. The result's sign cannot be assigned
from these facts alone. The same two undecoded words appear in multiplication's
zero handling at `#0359` and `#0361`, which is a concrete decoding lead.
[Recovered ROM words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L1447),
[partial J13 decoding](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1447).

No decoded exception-mask branch or rounding call occurs on the proposed
short zero routes. That supports checking for PC/RC independence and no fresh
PE, rather than treating either as an already measured result. Decode `6580`
and `6088`, including their tag and condition-code effects, before removing
the guard. Do not extend this branch to pseudozeros, inconsistent tags, empty
registers, denormals or special Y values.

The [sign-control follow-up](ZERO_SIGN_DATAPATH.md) records related words
decoded in the later addition report. They support a specific XOR-sign
hypothesis, while leaving destination routing and other write effects
unresolved. The logarithm guard remains in place.

## FYL2X at canonical +1

The FYL2X route begins at `#1260`. A candidate exact-power path runs through
`#1271` through `#1275`, calls J19 at `#0868`, swaps the temporary registers,
then reaches J13 through `#1269` and `#1270`. J19 contains an explicit creation
of zero `tmpB` at `#0885` when its exponent-difference test selects that case.
This is a useful lead, not a completed +1 trace.
[Range-reduction and exponent-splitting words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L869).

Resolving the candidate requires the opcode-selected comparison constant,
the shift output and zero predicates at `#1262`, `#1263` and `#1273`, and J19's
exponent conversion. The older generated condition labels are insufficient to
prove all those decisions. Even a confirmed zero temporary would still pass
through J13's unresolved sign operations. Do not implement this case as an
ordinary subtraction zero or copy later x87 exception behavior.

## Discriminating original-hardware captures

Use these twelve operand pairs, with all three documented PC settings, all
four RC settings and all eight TOP values, giving 1,152 initial cases:

- FYL2XP1: X is each of +0 and -0; Y is each of +1, -1, +0 and -0.
- FYL2X: X is +1; Y is each of +1, -1, +0 and -0.

Canonical raw encodings are: +0 `0000:0000000000000000`,
-0 `8000:0000000000000000`, +1 `3fff:8000000000000000`,
and -1 `bfff:8000000000000000`, with ZERO or VALID tags as appropriate.
Use distinct sentinel payloads in the remaining physical registers. Start with
clean flags and control word `00bf | (PC << 8) | (RC << 10)`.

Capture all raw register bytes, physical tags, TOP, status/control words and
instruction bookkeeping. Check the predicted single pop, result encoding and
new PE/UE without assuming their values. Repeat with PE unmasked, recording
the global interrupt mask and host interrupt setup, and with preexisting sticky
PE/UE in separate cases. Original Intel precision handling returns the rounded
result even when requesting an interrupt, so a modern before-exception rollback
must not be assumed. [Intel exception summary, S-20](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=703).

Chip markings, host configuration and completion or timeout observations must
accompany captures. This investigation establishes no exact cycle count and
found no attributable original-8087 capture resolving these cases.
