# Original 8087 finite addition and subtraction

Research date: 2026-10-20. This records the full J2 path for two nonzero finite
operands, including normal/unnormal combinations. It is a reconstruction from
Intel documents and the published die decode, not an original-hardware trace.
Raw arithmetic is substantially determined; exact-cancellation C1 and a few
control assumptions below still need independent confirmation.

## Representation and entry

Use the stored exponent and fraction without prenormalizing an unnormal.
For a VALID operand the magnitude is `S * 2^(E - 16383 - 63)`, with S the raw
64-bit significand. A clear integer bit does not make a VALID operand SPECIAL.
Intel's S-70 addition/subtraction rule preserves unnormal significance; S-71
gives the cancellation-zero sign rule. Neither page supplies a complete
bit-level algorithm for arbitrary raw unnormals.
[Intel S-70](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753),
[S-71](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=754).

The common arithmetic entry loads operands, applies its reverse-operand
selection, checks EMPTY/SPECIAL inputs, and calls J2. VALID unnormals do not
take SPECIAL_TMPS. A SPECIAL denormal first raises DE through J12; masked DE
changes its working E to 1 and tag to VALID, while unmasked DE exits without
performing J2. Keep this input preprocessing separate from internal alignment.
[Common entry](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html),
[SPECIAL_VAL](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

For an implementation, express FSUB/FSUBR as effective operand order and signs,
then use one signed-add procedure. This is a software representation of the
opcode/sign predicates, not a claim that hardware rewrites an input sign bit.

## Implementable J2 arithmetic

The following procedure follows the decoded branch structure. Keep range
completion separate, as described below. Addresses are decimal and the raw
snapshot is `745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

1. **Order by stored E.** At `#0237` through `#0245`, compare exponents and
   arrange A to have the larger stored exponent. Record `d = E_A - E_B` and
   the effective sign of A. This ordering is not a comparison of normalized
   numeric magnitudes. The sign latch is toggled where operand exchange or
   a negative subtraction requires it.
2. **Align B.** Produce its truncated fraction and GRS information for a
   right shift of d. A useful integer representation is
   `X = S_A << 3`, `Y = shift_right_jam(S_B << 3, d)`.
   Jam means OR all discarded bits into bit 0. This compact representation is
   conditional on resolving the exponent-dependent GRS construction, especially
   the d-at-least-64 path below. Carry/sign storage must extend beyond the
   67 fraction-plus-GRS positions.
3. **Same effective signs:** form `T = X + Y`. If the fraction has a carry
   beyond bit 63, right shift T once with jamming and record a one-bit exponent
   increase. Otherwise do not left-normalize. This applies to unnormal sums
   even when their result retains many leading zeros.
4. **Different effective signs:** form `D = X - Y`. If negative, negate it
   and flip the result sign. Set `T = abs(D)`. The fraction-zero branch and
   normalization eligibility below must be applied before final PC rounding.
5. **Round the resulting raw alignment.** Apply J11 with PC 24, 53 or 64.
   Preserve the chosen alignment and update the exponent only for the explicit
   shifts/carry described below. PE follows discarded GRS information and C1
   follows the magnitude increment on a J11 path.

This integer representation should be checked against the hardware's separate
GRS gates, not used as evidence of their implementation. Normal cancellation
requiring a long left shift has d at most 1, so alignment has lost no unknown
tail beyond those explicit bits. With d at least 2 and normal A, a nonzero
subtraction can need at most one left shift. With larger-E unnormal A, the
decoded normalization gate can prohibit that shift entirely.

[Raw J2 instructions](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L233).

## Alignment, retained shifter data and significance

`#0246` first right shifts zero by zero, initializing the right-shift read
result. J20 then handles d below 64 with a right shift at `#0528`, transferring
the aligned fraction to the B register at `#0529`. A complementary left shift
at `#0530` constructs the rounding tail at the opposite interface. For d at
least 64, the fraction becomes zero and only tail information survives.

The important consequence is that `#0257` reads the retained **aligned**
right-shift fraction. For the large-shift route it reads the zero initialized
by `#0246`. The addition report's comment calling this the original tmpB
should not be interpreted as restoring its unshifted fraction. The shifter
patent distinguishes the load circuit from the retained read latch at each
bidirectional interface; an opposite-direction load does not by itself replace
the previous right-shift read result.
[J20 sequence](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[Intel shifter, read/load circuits in figures 5 and 6](https://patents.google.com/patent/US4509144A/en),
[first-hand driver analysis](https://www.righto.com/2020/05/die-analysis-of-8087-math-coprocessors.html).

The resulting `#0258`/`#0259` normalization gate is:

```text
normalize_subtraction = integer_bit(S_A) OR integer_bit(aligned_fraction_B)
                     = integer_bit(S_A) OR (d == 0 AND integer_bit(S_B))
```

If false, `#0260` goes directly to sign/precision completion. If true, the
leading-zero circuitry shifts by whole zero bytes and remaining zero bits at
`#0282` through `#0288`, recording the total shift in tmpB's exponent field.
This is a significance limit, not permission to normalize every exact result.

The retained-latch interpretation follows the published raw sequence and
interface description, but lacks a captured internal trace. Mixed inputs whose
stored exponent ordering differs from their numeric magnitude are essential
discriminators. Intel's broad S-70 wording must not replace this raw branch
with an ordinary normalized magnitude comparison.

**Large-count GRS remains unresolved.** For d at least 64, `#0525` supplies a
zero fraction, `#0526` performs a left shift of zero places on the original
operand, and `#0532` loads rounding(H). This does not by itself prove the
generic-jam layouts for d=64 or d=65. The report's note 17 explicitly omits
exponent-converter-dependent distinctions between rounding-bit operations.
Keep this branch guarded until that circuit selection is decoded or original
hardware supplies discriminating traces. The d=64 result rows below are
conditional predictions of the mathematical GRS layout, not established
hardware expectations.

For a raw input fraction with no incoming GRS, the required ordinary alignment
layouts at the boundary are:

| d | Aligned fraction | G | R | Sticky source |
| --- | --- | --- | --- | --- |
| 63 | `S >> 63` | `S[62]` | `S[61]` | OR of `S[60:0]` |
| 64 | 0 | `S[63]` | `S[62]` | OR of `S[61:0]` |
| 65 | 0 | 0 | `S[63]` | OR of `S[62:0]` |
| 66 or more | 0 | 0 | 0 | OR of all S bits |

The d=63 row follows the ordinary J20 sequence: the complementary left shift
is zero, and the described rounding(H) extraction takes bits 62 and 61 and
ORs the remainder. For d=64, `#0526` also left shifts by zero, but the required
GRS row differs. It must therefore depend on exponent-converter-controlled
selection, not just the visible shifter output. Neither `main.cpp` nor the
recovered BC-operation notes decode raw `680a` at `#0532` into that truth table.
The patent gives the general GRS architecture but not these raw control-word
selectors. Rows 64 and above are requirements to discriminate, not a circuit
proof that the chip implements generic jamming there. In particular, the
possibility that the large-count path collapses a nonzero fraction to sticky
must be tested rather than assumed away.
[Rounding extraction and omitted details, note 17](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[raw J20](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L523).

## Cancellation and GRS-only differences

The test at `#0256` concerns the 64-bit fraction. If it is zero, `#0275` and
`#0276` expose the GRS bits for the `#0277` test.

- **All fraction and GRS bits zero:** take `#0261`. If A's raw integer bit is
  set, `#0263` transfers whole zero into tmpA, producing E zero and tag ZERO.
  If A's integer bit is clear, `#0262` skips that transfer: only the fraction
  is cleared at `#0265`, retaining E and VALID. That is a pseudozero.
- **Fraction zero but GRS nonzero, unnormal A:** `#0278` clears the fraction
  and `#0279` skips promotion. Keep the GRS information and original alignment
  for J11. It may round to a pseudozero or to the smallest raw fraction quantum.
- **Fraction zero but GRS nonzero, normal A:** decrement expAdj at `#0280`
  and load the bottom fraction bit, clearing the old GRS via the whole
  fraction/rounding destination at `#0281`. Then perform left normalization.
  In this reachable normal-input case, d is 1 and the surviving difference is
  the half-bit. Treating an arbitrary sticky tail as an exact half-bit would
  incorrectly extend the branch beyond its input constraints.

The exact-cancellation result sign is negative only for RC down. `#0264`
first clears tmpA's sign. `#0266`, raw `e05b`, returns if condition `2d` is
false; otherwise `#0267` sets the sign. Condition `2d` is distinct from J11's
GRS-dependent increment condition `1d`. The same `2d` chooses the finite
directed-overflow response at `#0349`, supporting the predicate
`(RC=down AND sign=positive) OR (RC=up AND sign=negative)`. With tmpA made
positive, this reduces to RC down. At exact cancellation GRS has already
tested zero, so an inexactness requirement cannot explain the negative-zero
case. This is a cross-path derivation consistent with Intel S-71; a complete
published transistor-level truth table for `2d` was not found.
[Cancellation words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L262),
[directed overflow rules, S-75](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=758).

## Rounding carry and exact exponent arithmetic

The initial exponent-adjustment counter is biased: expAdj starts at 1, while
the multi-bit normalization shift starts at 0. The final exponent calculation
is exactly:

```text
E_result = E_A - normalization_shift + expAdj - 1
```

`#0293` subtracts tmpB's normalization count without carry-in, accounting for
the minus 1. `#0295` adds expAdj. An addition carry increments expAdj at
`#0273`; GRS-only half-bit promotion decrements it at `#0280`.

J11 overflow of a rounded fraction is also ordinary renormalization.
`#0303`, raw `0625`, transfers zero with the source top-bit modifier into the
sum register, supplying `8000000000000000`. `#0304` increments expAdj.
The constant is not an infinity by itself: that would also require an
all-ones exponent and SPECIAL tag. The source modifier and destination
definitions resolve the addition report's concern about J11 returning infinity
on an ordinary fraction carry. The final exponent calculation makes this
JBIT at E+1.
[Raw J11 words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L300),
[register and source-modifier definitions](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L15).

The raw-grid PC rounding rule, PE/C1 relationship, carry into the integer bit
and zero-rounded VALID tag are detailed in
[UNNORMAL_REDUCED_PRECISION.md](UNNORMAL_REDUCED_PRECISION.md). Intel's patent
connects P to OR(G,R,S) at rounding and specifies result delivery even when
precision is unmasked. [Intel rounding circuitry](https://patents.google.com/patent/US4338675A/en).

## Open boundaries

**Exact-cancellation C1 remains unresolved.** The decoded common entry contains
no explicit C1 clear, and the cancellation tail returns before J11's documented
C1 update. That does not prove preservation: an instruction-start action,
shared sign-latch storage or another implicit write needs circuit evidence.
Seed both C1 states and both operand signs in captures. Do not assign C1 from
J11 when J11 did not execute. The provisional condition table and generated
listing are not an exhaustive control-register write specification.
[C1 mechanism and limitations](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[provisional condition descriptions](https://www.righto.com/2025/12/8087-microcode-conditions.html).

J2 returns the raw result to the common range check at `#0010`. Underflow,
overflow, mask handling and exponent wrapping then belong to J16. They must
retain their independent guards until the corresponding unnormal range paths
are resolved. A normalizing backend's range result is not sufficient evidence.
This note also does not settle pseudozero inputs, inconsistent tags or the
electrical timing of the datapath.

## Literal discriminating cases

Use VALID tags for the nonzero inputs, PC64 and nearest/even unless stated.
Results below are predictions from the decoded path, pending hardware capture.
The d=64 rows additionally depend on the unresolved large-count GRS mapping.
All non-cancellation rows should finish through J11 with C1 clear and no new
PE unless the row says otherwise.

| Operation and raw operands, `E:S` | Predicted raw result | Discriminator |
| --- | --- | --- |
| `4000:2000000000000000 + 4000:2000000000000000` | `4000:4000000000000000`, VALID | Same-E unnormal sum stays unnormal. |
| `4000:4000000000000000 + 4000:4000000000000000` | `4000:8000000000000000`, VALID | Integer bit becomes set without changing E. |
| `4000:c000000000000000 + 4000:6000000000000000` | `4001:9000000000000000`, VALID | True carry shifts right and increments E. |
| `4000:6000000000000000 - 4000:2000000000000000` | `4000:4000000000000000`, VALID | Same-E unnormal subtraction skips left normalization. |
| `4000:8000000000000000 - 4000:4000000000000000` | `3fff:8000000000000000`, VALID | A normal operand permits left normalization. |
| `4001:6000000000000000 - 3fff:8000000000000000` | `4001:4000000000000000`, VALID | Aligned smaller normal no longer has its integer bit set. |
| `4001:1000000000000000 - 3fff:8000000000000000` | `c001:1000000000000000`, VALID | Negative difference despite larger stored E for A. |
| `3fff:8000000000000000 - 3ffe:ffffffffffffffff` | `3fbf:8000000000000000`, VALID | GRS-only half-bit promotion, then long normalization. |
| `403f:0000000000000001 - 3fff:8000000000000000` | `403f:0000000000000000`, VALID; PE set, C1 clear | d=64, raw half-quantum rounds to pseudozero. |
| Previous row with RC up | `403f:0000000000000001`, VALID; PE and C1 set | Directed rounding uses the original raw quantum. |
| `4000:4000000000000000 - 4000:4000000000000000` | `4000:0000000000000000`, VALID | Exact unnormal cancellation yields pseudozero. C1 unresolved. |
| Previous row with RC down | `c000:0000000000000000`, VALID | Cancellation sign rule applies to the retained-E pseudozero. |
| `3fff:8000000000000000 - 3fff:8000000000000000` | `0000:0000000000000000`, ZERO | Normal cancellation takes the whole-zero transfer. C1 unresolved. |

Extend these with d equal to 0, 1, 2, 63, 64, 65, 66 and a large value;
tails below, at and above each PC half-way point; both operand orders and
signs; FSUBR; and all TOP values. Compare raw bytes, tags, sticky flags, C1,
and completion state. A numeric-only comparison misses the significance
behavior this algorithm is intended to reproduce.

## Implementation checkpoint

`src/original_add.c` represents the fraction and GRS separately, preserving the
decoded ordering, normalization gate, GRS-only branch and rounding carry.
The instruction layer uses this subsystem for properly tagged nonzero finite
operands in the current safe stored-E interval, `0041` through `7ffd`, or a
canonical zero, when at least one input is unnormal. It keeps fresh DE and
reserved PC outside this path. The bounded unnormal-plus-zero results remain
unchanged.

Nonzero shifts of at least 64 return an unresolved-alignment status. Exact
cancellation returns its predicted payload with an unresolved-C1 status.
Neither status is accepted as a strict-mode result. Unsupported unnormal range
completion also retains its guard. A correct finite payload alone does not
establish correct condition bits or exception continuation.

[Validation](../TESTING.md#original-finite-add-datapath) includes literal raw
results, an independent Fraction calculation and a masked capture campaign.
The campaign includes the unresolved cancellation and large-count branches so
physical evidence can distinguish them. No hardware pass or issue completion
is inferred from model-only tests.
