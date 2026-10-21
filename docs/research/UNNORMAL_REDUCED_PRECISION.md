# Reduced precision for original 8087 unnormal arithmetic with zero

Research date: 2026-10-20. This extends the bounded nonzero-unnormal case in
[UNNORMAL_ZERO_ARITHMETIC.md](UNNORMAL_ZERO_ARITHMETIC.md) to PC 24 and PC 53.
It derives expectations from Intel documentation, the processor patent and
decoded die microcode. It supplies no original-hardware measurement.

## Finding and scope

The published decode supports rounding on the **stored significand alignment**,
including inexact results. It supports both an increment into the integer bit
and a zero significand retaining its original nonzero exponent and VALID tag.
It does not support normalizing the unnormal before rounding.

Keep the same conservative input boundaries:

- Exactly one canonical signed zero, with exponent/significand zero and tag ZERO.
- One nonzero finite unnormal, `0 < S < 8000000000000000`, tagged VALID.
- Its stored exponent E, excluding sign, is from `0041` through `7ffd`.
- PC is 24 or 53. Operands are present and execution is not already blocked.
- No fresh denormal-operand event has occurred during a memory load.

This is enough to extend the guarded arithmetic path within these boundaries.
The resulting zero payload is an output pseudozero, not permission to accept
pseudozero inputs under the same shortcut. Confidence is document/die-derived;
an identified physical 8087 remains the independent validation target.

## Raw rounding rule

After applying the opcode's operand order and subtraction sign, let s be the
result sign inherited from the nonzero unnormal. Define:

```text
d = 64 - PC                  40 for PC24, 11 for PC53
U = 2^d
q = floor(S / U)
r = S modulo U
```

Increment q under these conditions:

| RC | Magnitude increment |
| --- | --- |
| Nearest/even | `r > U/2`, or `r == U/2` and q is odd |
| Down | r is nonzero and s is negative |
| Up | r is nonzero and s is positive |
| Chop | Never |

The result fraction is `(q + increment) * U`, with the original E and the
operation-adjusted sign. Fresh PE is set exactly when r is nonzero. C1 records
the increment, including zero when no increment occurs. C0, C2 and C3 and
preexisting sticky exception flags are preserved. There is no new DE, UE or OE
within this input scope.

When r is zero, the low 40 bits at PC24 or low 11 bits at PC53 are clear.
Every RC then preserves S exactly, with no new PE and C1 clear.

## Why rounding uses raw alignment

Intel S-17 explains that leading zeros reduce the available significance of
nonnormal numbers. S-18 says reduced precision clears unused fraction bits on
the right. S-70 preserves unnormal significance in addition/subtraction; its
normalization rule does not require making every result canonical. S-71's
zero rules establish the nonzero operand's signed contribution.
[Intel S-17](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=700),
[S-18](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=701),
[S-70](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753),
[S-71](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=754).

The J2 argument alignment produces the original S with zero extra rounding
bits. Its addition route skips normalization without a carry, and its
subtraction route skips normalization when both aligned raw integer bits are
clear. Both reach J11 at `#0290`. Thus J11 receives S in its original stored
alignment. [First-hand J2 decode](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

J11 selects a fixed count of 23 or 52 at `#0306`, produces discarded-bit
information at `#0307` through `#0309`, shortens at `#0310` and `#0311`,
conditionally increments at `#0312` and `#0313`, then restores the original
alignment at `#0314` and `#0315`. It contains no leading-zero normalization.
The die report identifies the rounding decision's C1 write.
[ADJUST_PRECISION and its C1 side effect](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

The report notes an unresolved general significand-overflow detail in J11.
That detail does not arise here: since S is below the integer bit, the largest
possible rounded value is exactly that bit, not the extra carry bit at position
64 tested by `#0316`.

## PE and the precision mask

Intel patent US4338675A, rounding section E and its exact-arithmetic discussion,
explicitly connects the P error bit to OR(G,R,S), describes the four increment
decisions, and says an inexact result is delivered with P set. PM clear adds
an interrupt request. Together with J11's discarded-bit construction, this
supports PE for nonzero r, including an even tie that does not increment.
It does not depend on first interpreting S as a canonical IEEE operand.
[Intel rounding circuitry and P flag](https://patents.google.com/patent/US4338675A/en).

The original manual also classifies precision as an after exception and gives
a rounded result for both masked and unmasked precision. Preserve the result
and instruction's normal pop when requesting a precision interrupt. Host
interrupt delivery still depends on the original interrupt setup.
[Intel S-19](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=702),
[S-20](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=703).

## Exponent, carry and zero-tag propagation

Addresses are decimal. The recovered snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

J11 does not increment the exponent-adjustment counter unless its extra carry
path is reached. Rounding S into `8000000000000000` reaches bit 63, so this
path is skipped. The exponent remains E and the payload becomes a normal
value with the already-VALID tag.

For a rounded fraction of zero, the nonzero-input cancellation test occurred
earlier, at `#0256`; it is not repeated after J11. `#0291` writes fractionA
only. `#0297` writes exponentA only. The recovered register definitions
distinguish these destinations from whole tmpA, which includes its tag.
Neither write clears or recomputes the VALID tag.
[Destination definitions](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L30),
[J2 completion words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L290).

The alignment shift did not modify the unnormal, tmpB's exponent adjustment
is zero at `#0249`, and the biased exponent-adjustment counter has its initial
value. The subtraction without carry at `#0293` followed by the addition at
`#0295` therefore restores E. No normalization adjustment has occurred.
The safe E interval avoids the common range routine at `#0010`; `#0012` or
`#0014` writes whole tmpA with its unchanged tag.
[Adjustment-counter convention and common write](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html),
[raw completion words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L290).

Consequently a zero-rounded fraction produces `E:0000000000000000`, still
VALID and retaining s. It is a pseudozero by Intel's S-70 definition. Applying
the canonical cancellation-zero sign rule or changing its tag to ZERO would
insert a path absent from the decoded sequence. This is a concrete prediction
for hardware capture, not a claim that those captures already exist.

## Literal discriminating vectors

All rows use FADD with positive unnormal `4000:S`, tag VALID, plus canonical
+0, tag ZERO, nearest/even rounding and initially clear PE. E stays `4000`
and the output tag stays VALID, including rows with zero fraction.

| PC | Input S | Output fraction | Fresh PE | C1 |
| --- | --- | --- | --- | --- |
| 24 | `4000010000000000` | `4000010000000000` | 0 | 0 |
| 24 | `4000007fffffffff` | `4000000000000000` | 1 | 0 |
| 24 | `4000008000000000` | `4000000000000000` | 1 | 0 |
| 24 | `4000018000000000` | `4000020000000000` | 1 | 1 |
| 24 | `4000008000000001` | `4000010000000000` | 1 | 1 |
| 24 | `7fffff8000000000` | `8000000000000000` | 1 | 1 |
| 24 | `0000008000000000` | `0000000000000000` | 1 | 0 |
| 24 | `0000008000000001` | `0000010000000000` | 1 | 1 |
| 53 | `4000000000000800` | `4000000000000800` | 0 | 0 |
| 53 | `40000000000003ff` | `4000000000000000` | 1 | 0 |
| 53 | `4000000000000400` | `4000000000000000` | 1 | 0 |
| 53 | `4000000000000c00` | `4000000000001000` | 1 | 1 |
| 53 | `4000000000000401` | `4000000000000800` | 1 | 1 |
| 53 | `7ffffffffffffc00` | `8000000000000000` | 1 | 1 |
| 53 | `0000000000000400` | `0000000000000000` | 1 | 0 |
| 53 | `0000000000000401` | `0000000000000800` | 1 | 1 |

The even-tie rows distinguish raw alignment from canonical normalization:
their normalized numeric values are exactly representable at the corresponding
canonical precision, while the original raw-grid path discards a bit and sets
PE. Directed modes, negative signs and subtraction must use s from the
operation, rather than the input byte sign before subtraction adjustment.

Capture these literals first, then cross signs, zero signs, operand order,
FADD/FSUB/FSUBR, RC and TOP. Include both clean and preset C1, sticky PE/UE,
and precision-mask settings. Compare all raw payloads and tags, not only the
numeric value printed by a later floating-point implementation.

Keep guards for pseudozero inputs, denormals, inconsistent tags, missing
operands, reserved PC and excluded E values. Reaching a normal or pseudozero
output inside this bounded path does not remove those input boundaries.

## Implementation and validation

The instruction layer applies this rule before canonical backend arithmetic.
It preserves the backend's mathematical-oracle role and the original operand
order, subtraction sign, destination and pop handling. A fresh memory-load DE
excludes this path; the before-operation exception keeps its existing priority.

[The test matrix](../TESTING.md#bounded-unnormal-plus-zero-arithmetic) uses these
literal results across register and memory forms. The capture generator also
includes them as masked inputs, with a signed Fraction calculation checking
model replay on the raw grid. The generator supplies no expected hardware
capture. The original exponent and VALID tag are checked separately from the
rounded numeric fraction.
