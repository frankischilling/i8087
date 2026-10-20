# Original constant-load paths and remaining bit evidence

Research date: 2026-10-20. Scope: FLDL2T, FLDL2E, FLDPI, FLDLG2 and FLDLN2.
No physical 8087 execution trace was available.

## Finding

The recovered load path supports fixed 64-bit constants that do not depend on
PC or RC and do not raise a new precision exception on a successful push.
It does not execute the precision-adjustment routine used by arithmetic.

The five current 80-bit literals have **not** been independently verified here
against every bit of the original constant ROM. Keep `I8087_GAP_CONSTANT_ROM`.
Mathematical nearest rounding, approximate decimal tables and a modern x87 run
are insufficient to certify the original payload.

## What the physical ROM report establishes

Shirriff extracted constants from a decapped 8087. His report describes a
134-row, 21-column ROM, with two interleaved sets of values. A missing 6-by-6
corner means the first 12 non-CORDIC constants have only 64 stored bits;
the CORDIC constants use 67. The user-loadable constants belong to the first
group. This distinction matters: the general 67-bit datapath does not imply
that these load instructions must round 67-bit operands.
[Physical extraction, especially notes 5 and 8](https://www.righto.com/2020/05/extracting-rom-constants-from-8087-math.html).

The public table gives decimal approximations and interpreted mathematical
meanings, rather than complete hexadecimal payloads. In that investigation
the exponents were inferred separately; the report did not yet identify the
exponent ROM. The published images are primary physical evidence, but this
research has not completed a reliable bit-by-bit transcription and column
selection map for all five constants. The linked partial ROM view and whole
die image are therefore leads for further extraction, not verified bytes:
[ROM view](http://static.righto.com/images/8087-constants/rom-overview.jpg),
[whole stripped die](http://static.righto.com/images/8087-constants/stripped-big.jpg).

## Recovered instruction path

Addresses are decimal. The inspected granite snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

| Addresses | Decoded work |
| --- | --- |
| `#1003` through `#1007` | FLD1, FLDL2T, FLDL2E and FLDPI select exponent constant index 5 and conditionally increment it using the low opcode bit. |
| `#1008` through `#1012` | FLDLG2 and FLDLN2 select exponent constant index 11 and conditionally decrement it. |
| `#1013` | Copy the exponent result to raw sign/exponent storage in `tmpA`. |
| `#1014` | Copy the selected fraction constant directly to `fracA`. |
| `#1015` through `#1017` | Push and check stack availability. |
| `#1018`, `#1019` | Copy `tmpA` to ST(0), then finish. |
| `#1022` through `#1027` | Invalid-operation response for a full stack. |

[Recovered constant-load listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1004).

No successful-path branch tests PC or RC; no J11/ADJUST_PRECISION call or
precision-exception microinstruction appears. This supports invariance of the
loaded bits under the legal PC/RC settings. It remains a decoded-path inference,
subject to correct identification of the constant-selection and transfer
circuitry. The older listing explicitly leaves some operations unresolved.
[Interpretation notes](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md).

Shirriff's later instruction-decoding analysis independently explains the
separate fraction ROM, exponent ROM and opcode-based constant PLA. It also
identifies the two load entry groups and the exponent adjustment. That supports
the path interpretation without supplying a complete numerical dump of the
fraction ROM. [Instruction-decoding analysis](https://www.righto.com/2026/02/8087-instruction-decoding.html).

## Original Intel checks

The 1981 supplement, printed S-38, describes the constants as having full
temporary-real precision of 64 bits. S-51 lists invalid operation as the
exception for these five instructions and does not list precision. Those
statements agree with a direct constant transfer and a stack-overflow check.
The relevant pages were checked in the scan. [Intel S-38](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=721),
[S-51](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=734).

A later Intel Pentium manual states that its round-to-nearest constant loads
produce the same constants as the 8087 and Intel287. This is a manufacturer
compatibility statement that can corroborate candidate values. It does not
make the later processor's RC-dependent loading behavior original-8087
behavior, nor identify the stepping of the photographed 8087. [Intel Pentium
manual, section 6.3.6](https://www.ardent-tool.com/CPU/docs/Intel/Pentium/241430-004.pdf).

## Candidate bytes awaiting independent confirmation

These are the current repository literals, not newly captured or optically
verified outputs. All signs are positive. The exponent assignments agree with
the documented constant magnitudes and the decoded adjustment groups.

| Instruction | Opcode | Candidate sign/exponent | Candidate significand |
| --- | --- | --- | --- |
| FLDL2T | `D9 E9` | `0x4000` | `0xd49a784bcd1b8afe` |
| FLDL2E | `D9 EA` | `0x3fff` | `0xb8aa3b295c17f0bc` |
| FLDPI | `D9 EB` | `0x4000` | `0xc90fdaa22168c235` |
| FLDLG2 | `D9 EC` | `0x3ffd` | `0x9a209a84fbcff799` |
| FLDLN2 | `D9 ED` | `0x3ffe` | `0xb17217f7d1cf79ac` |

Each low bit must have evidence. Agreement with the mathematical constant to
about 19 decimal digits does not identify every possible 64-bit significand.

## Exact capture campaign

For each opcode, restore each TOP value 0 through 7 in turn, tag word `0xffff`
(empty stack), clear exception flags, clear IR/BUSY, and known raw register
storage. Use
`CW = 0x00bf | (PC << 8) | (RC << 10)`, where PC is 0, 2 or 3 and RC is 0,
1, 2 or 3. This makes 480 independent inputs with all exceptions masked.
Do not include reserved PC=1 in the documented-mode verdict.

| PC | Nearest RC=0 | Down RC=1 | Up RC=2 | CHOP RC=3 |
| --- | --- | --- | --- | --- |
| 24 bits, PC=0 | `0x00bf` | `0x04bf` | `0x08bf` | `0x0cbf` |
| 53 bits, PC=2 | `0x02bf` | `0x06bf` | `0x0abf` | `0x0ebf` |
| 64 bits, PC=3 | `0x03bf` | `0x07bf` | `0x0bbf` | `0x0fbf` |

For initial TOP=t, the decoded-path expectations are: TOP becomes
`d = (t + 7) & 7`, the physical tag word becomes `0xffff & ~(3 << (2*d))`,
the new ST(0) bits remain identical across all control settings and TOP values
for a given instruction, and initially clear PE remains clear. For example,
initial TOP=0 produces TOP=7/tag=`0x3fff`, while TOP=1 produces
TOP=0/tag=`0xfffc`. Compare the
full 80-bit output, especially the lowest bits. Treat the candidate payload table as a
candidate to test, not an authenticated oracle. Avoid an intervening FST32 or
FST64 that would discard the bits under investigation.

Repeat with initial PE set to check sticky-flag preservation, and with the
destination physical register occupied to exercise the separate invalid path.
Record condition bits separately; this note does not establish their exact
successful-load values. An unmasked-precision campaign requires a fixture that
permits unmasked tests; the current hardware collector deliberately restricts
inputs to all exceptions masked.

Before removing the guard, obtain a complete attributable ROM transcription
with its bit/column mapping and constant-PLA selection, or original-hardware
raw outputs for the candidates and mode matrix. Preserve specimen identity and
repeat on independently sourced parts. The current evidence narrows the gap to
payload confirmation and remaining transfer/status details; it does not close
it or establish cycle accuracy.
