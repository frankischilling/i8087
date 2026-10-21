# Original 8087 addition and subtraction with an unnormal and zero

Research date: 2026-10-20. This note distinguishes a bounded nonzero-unnormal
case from pseudozeros, denormals and reduced-precision arithmetic. Expectations
below come from original Intel rules and recovered die microcode. No original
hardware trace was available to confirm them.

## Bounded result supported by the sources

At PC 64, one canonical ZERO-tagged operand and one nonzero finite unnormal
tagged VALID can retain the unnormal's raw exponent and significand. Use this
conservative initial scope:

- One operand has exponent and significand zero, either sign, and tag ZERO.
- The other has `0 < significand < 8000000000000000`, tag VALID, and stored
  exponent from `0041` through `7ffd`, inclusive, excluding its sign bit.
- Operands are present and no unrelated pending exception prevents execution.

FADD returns the unnormal's sign. Subtraction preserves that sign when the
unnormal is the minuend and flips it when the unnormal is the subtrahend.
The zero's sign does not change this nonzero result. The result stays VALID,
raises no new DE, PE, UE or OE, and has no rounding increment. J11 updates C1
to zero; this path contains no update of C0, C2 or C3. Preserve preexisting
sticky exception flags. Instruction-pointer, destination and pop bookkeeping
still follow the selected opcode.

This is a narrowly supported arithmetic path, not a raw-transfer early exit.
It requires preserving unnormal representation in the instruction layer or an
appropriate backend path. Simply permitting the existing normalizing `sf_add`
does not reproduce it.

## Original Intel rules

Printed S-69 says an unnormal has a clear integer bit and a VALID tag. Table
S-25 on S-70 says the operand with greater absolute value determines the
normalization of an addition/subtraction result. Table S-26 on S-71 gives the
nonzero operand's signed value for addition or subtraction with true zero;
footnote 6 identifies the sign as the sign of that nonzero operand, with the
subtraction direction accounted for in the table.
[Intel S-69](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=752),
[S-70](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753),
[S-71](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=754).

These rules support unnormal preservation, but do not alone prove that reduced
PC settings leave every raw bit unchanged. The recovered execution path
explains that distinction.

## Recovered path and absence of a DE conversion

Addresses are decimal. The inspected snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

Shirriff's addition report identifies the common `#0008` anomalous-operand
test as checking for an EMPTY or SPECIAL temporary. ZERO and VALID do not
trigger SPECIAL_TMPS. The bounded input therefore bypasses the denormal
conversion and enters J2 at `#0232` directly. Its early return at `#0234`
requires both operands to be ZERO-tagged and does not apply here.
[Common arithmetic entry and J2](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

Exponent comparison and temporary exchanges at `#0237` through `#0248`
arrange to align the smaller-exponent operand. Here it is the canonical zero,
with stored exponent zero. CREATE_DENORM therefore aligns a zero significand,
not the unnormal. Its name does not mean that it signals a denormal-operand
exception.

The exponent difference is at least 65 in the initial scope. In J20, `#0524`
selects the large-shift route, `#0525` supplies zero to the internal B register,
and `#0526` passes the zero through the shifter for discarded-bit handling.
`#0532` supplies zero rounding bits. There is no exception operation in J20.
The unnormal's original significand survives in the other temporary.
[CREATE_DENORM decode](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

## Why the representation survives at PC 64

The unsigned-add route at `#0269` has no carry when adding aligned zero, so
`#0270` skips the right normalization shift. On the unsigned-subtract route,
the nonzero difference does not take the cancellation-zero path. Both tested
raw top bits at `#0258` and `#0259` are clear: one belongs to the unnormal and
the other to the aligned zero. `#0260` therefore skips left normalization.
The sign latch tracks operand exchange and subtraction direction.
[J2 normalization branches](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

Both routes reach `#0289` and the J11 call at `#0290`. PC 64 selects `#0300`;
with no discarded bits it returns without incrementing, while updating C1.
The common exponent adjustment retains the original exponent when no
normalization shift or carry occurred. The result is written through the
normal arithmetic completion path, not through a copy instruction.
[Precision adjustment and C1](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[raw J2 completion words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L290).

The lower bound avoids underflow even for significand 1, whose leading bit is
63 places below the usual integer bit. Stored exponent `0040` with significand
1 is the exact minimum-normal value; it is deliberately outside the initial
scope. The upper bound also leaves room below the largest stored exponent.
No range adjustment is needed for the bounded result.

## Cases that still need separate work

- **PC 24 and PC 53:** J11 takes its shortening and rounding path. A raw copy
  is not supported for arbitrary significands. Rounding is applied to the
  existing raw alignment, so normalizing first can change both retained bits
  and PE behavior.
- **Pseudozero:** Its significand is zero but its VALID tag is not ZERO.
  It misses the initial two-zero branch and can enter cancellation handling
  or the ordinary add path. Do not reuse the nonzero result rule above.
- **Denormal:** A SPECIAL-tagged denormal enters SPECIAL_TMPS. J12 raises DE;
  if masked, it changes the working exponent to 1 and tag to VALID before
  resuming. If DE is unmasked, execution stops through the denormal interrupt
  path. This differs from aligning an internal canonical zero.
- **Range edges and inconsistent tags:** Keep guarded until their completion,
  zero-result and exception paths have been checked explicitly.

[Denormal conversion, SPECIAL_VAL](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[Intel denormal rules, S-69](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=752).

## Discriminating vectors

Use these positive raw unnormals, then repeat with their sign bit set:

| Stored exponent | Significand | Purpose |
| --- | --- | --- |
| `4000` | `4000000000000000` | Equivalent normal value +1, but raw representation must survive. |
| `4000` | `0000000000000001` | Many leading zeros expose normalization. |
| `0041` | `0000000000000001` | Conservative low-exponent boundary. |
| `7ffd` | `4000000000000001` | Large exponent and a significant low raw bit. |

Cross these four payloads with both unnormal signs, both zero signs, both
operand orders, FADD/FSUB/FSUBR, all RC settings and all TOP values. At PC 64,
that gives 3,072 cases. Start with `D8 C1`, `D8 E1` and `D8 E9`, using correctly
tagged ST(0) and ST(1), clean exceptions and control word `03bf | (RC << 10)`.
Seed C1 set to verify that exact arithmetic clears it. Repeat with other sticky
flags and condition-code patterns separately.

Expected bytes copy the unnormal exponent/significand with the sign rule above.
For example, `4000:4000000000000000` plus +0 must remain that raw value;
`3fff:8000000000000000` would expose an unwanted normalization. Add negative
tests confirming that reserved PC, pseudozeros, denormals and excluded exponent
edges remain guarded. Treat these as document/die-derived expectations until
an identified original chip supplies independent captures.

## Implemented scope and tests

The instruction layer checks both tags, canonical zero bits, nonzero unnormal
significand, documented PC and the stated exponent interval before applying this result.
A new denormal-operand event excludes the path, preserving the separate memory
load response. Subtraction sign adjustment and reverse-operand selection occur
before the bounded result is accepted. Ordinary destination, pop and exception
bookkeeping still applies.

The backend's canonical arithmetic remains unchanged. Its normalization is
useful for the independent mathematical oracle but cannot represent the raw
unnormal result here. [Regression scope](../TESTING.md#bounded-unnormal-plus-zero-arithmetic)
and the `unnormal-zero` input campaign distinguish that boundary. The capture
generator implements the 3,072-case PC64 matrix above. The
[reduced-precision extension](UNNORMAL_REDUCED_PRECISION.md) adds PC24/53 raw-grid
rounding, carry and pseudozero-output cases; the combined campaign has 15,360
inputs. Its checks are model-only.
