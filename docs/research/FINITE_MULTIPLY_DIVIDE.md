# Original 8087 finite multiplication and division

Research date: 2026-10-20. This derives the finite J1 and J3 datapaths from
original Intel rules and the published die decode. It supplies implementable
integer formulas without requiring physical hardware or a complete transistor
reconstruction. It also identifies a specific conflict between the literal
normalization decode and Intel's broad unnormal rule. These are source-derived
expectations, not measured silicon results.

## Representation and source boundaries

For a nonzero VALID input, use its stored exponent E and its raw unsigned
64-bit significand S. Do not normalize an unnormal input first:

```text
value = (-1)^sign * S * 2^(E - 16383 - 63)
```

Intel S-70 Table S-25 says multiplication with an unnormal operand gives an
unnormal result. Division permits an unnormal dividend, with an unnormal
result, and rejects an unnormal divisor. The original processor patent
independently describes this significance policy. It does not specify the
complete raw algorithms.
[Intel S-70, PDF ordinal 753, index 752](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753),
[Intel patent US4338675A, denormalized operands and Table 2](https://patents.google.com/patent/US4338675A/en).

The recovered snapshot is commit
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`. All microcode addresses below are
decimal. Its generated disassembly contains provisional labels. Use the newer
first-hand adder and J11 descriptions to interpret raw arithmetic operations,
the exponent-adjustment register and exception flags.
[Raw J1 and J3](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L360),
[current adder interpretation](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html),
[J11 interpretation](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

The ordinary finite scope here excludes ZERO-tag operands, pseudozero inputs,
SPECIAL values and unnormal divisors. Denormal inputs require their original
exception preprocessing. Range completion after the arithmetic return is a
separate J16 path. An initial implementation can accept finite results whose
computed stored exponent stays in range, then extend range completion under
its own evidence.

## Common completion

Both routines finish a fraction, call J11, store the fraction separately, and
then calculate the exponent. They contain no leading-zero normalization loop.
Their common arithmetic caller checks range at `#0010`, writes the selected
destination register, and performs the opcode's pop. Both finite operation
signs are the exclusive-or of the effective operand signs. FDIVR changes the
effective operand order before J3.

The raw sign word `6580` is shared by J1 and J3. Its complete control encoding
is not needed to infer the documented finite multiplication/division sign
rule, and this does not resolve its separate zero-tag behavior.
[Common arithmetic flow and sign cases](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

## Multiplication sequence and exponent

J1 starts at `#0359`. Ignoring the excluded zero-tag returns, its numerical
preparation is:

- `#0365` through `#0373` choose a full multiplier or a shortened multiplier.
  The full route loads all of tmpA fraction and loop count 31. The short
  route shifts that fraction right by 40 and loads count 11.
- `#0374` loads tmpB fraction into the B register with rounding storage.
  Raw `6948` selects multiplication mode at `#0375`.
- `#0376` clears the signed sum and rounding storage. Raw `52c0`, `f40c`
  and `524c` at `#0377` through `#0379` run the hardware multiplication.
- `#0380` tests condition `33`. The clear-bit route shifts the sum with
  rounding storage at `#0381`, transfers the shifter result at `#0382`, and
  decrements expAdj at `#0383`. J11 is called at `#0384`.

The first-hand datapath report identifies radix-4 Booth multiplication and
the two-bit sum shift. This agrees with 32 full multiplier digit steps and
12 shortened steps. Condition `2a`, which selects the shortened route, is
still only partly described in the condition table. A low-40-zero test would
make the shortened route exact, but do not turn that inference into an
established cycle selector. A full unsigned product can implement the
numerical result while leaving this timing choice explicit.
[Booth datapath](https://www.righto.com/2026/06/intel-8087-adder-reverse-engineered.html),
[provisional condition table](https://www.righto.com/2025/12/8087-microcode-conditions.html).

The exponent calculation is less ambiguous. `#0386` selects bias `3fff`;
`#0387` through `#0390` add the input exponents and subtract the bias with
carry-in one. `#0391` and `#0392` add expAdj. Therefore:

```text
E_mul = E_A + E_B - 16383 + expAdj
```

The first-hand addition analysis establishes that hardware initializes
expAdj to 1. The optional one-bit left shift decrements it; J11 fraction
overflow increments it. For ordinary normal multiplication, this identifies
the pre-normalization binary alignment as `P / 2^64`, where `P = S_A*S_B`.
For example, two significands `8000000000000000` need a one-bit left shift
and exponent adjustment zero; two significands `c000000000000000` need no
left shift and exponent adjustment one.

## Division sequence and exponent

J3 starts at `#0410`. `#0412` loads the dividend fraction into the signed
sum, `#0413` loads the divisor fraction into the B register, and `#0414`
selects division mode with raw `6a48`. `#0415` loads loop count 63. Raw
`52c0`, `5249`, `f40c`, `524a` and `5244` at `#0416` through `#0420`
perform the division sequence and correction. `#0421` issues raw `e400`;
`#0422` transfers the quotient register to the sum.

`#0423` then tests the same condition `33`. The following three instructions
are the same shift, transfer and expAdj decrement used by J1. J11 runs at
`#0427` for ordinary FDIV; its conditional exclusion concerns BCD processing,
not the finite FDIV opcode.

The patent describes a non-restoring divider with sign-selected add/subtract,
a quotient shift register, two further quotient bits for guard and round,
and sticky from the remaining remainder. A bit-serial integer divider can
produce equivalent quotient and remainder information without reproducing
the hardware's non-restoring intermediate signs. The latter still matter to
cycle modeling.
[Intel divider and rounding architecture, sections B and E](https://patents.google.com/patent/US4338675A/en).

`#0431`, raw `4808`, subtracts the divisor exponent without carry-in, giving
`E_A - E_B - 1`. Adding bias and expAdj at `#0432` through `#0435` gives:

```text
E_div = E_A - E_B + 16383 + expAdj - 1
```

Calibration with exact normal `1/1` identifies the initial quotient alignment
as `(S_A/S_B) * 2^63`, with expAdj one. A one-bit left shift changes this to
`(S_A/S_B) * 2^64` and expAdj zero. There is no permission to continue shifting
until an unnormal dividend's quotient becomes normal.

## Implementable exact integer formulas

The following is the literal top-bit interpretation of the published path.
It is established for normal inputs and is a concrete reconstruction to test
for unnormal inputs. The unnormal crossing conflict below must remain visible.

For multiplication, compute the full unsigned 128-bit P and select:

```text
delta = 1 if P >= 2^127 else 0
E = E_A + E_B - 16383 + delta
exact_raw_fraction = P / 2^(63 + delta)
```

For division by a normal divisor, select:

```text
delta = 0 if S_A >= S_B else -1
E = E_A - E_B + 16383 + delta
exact_raw_fraction = (S_A * 2^(63 - delta)) / S_B
```

Apply the requested PC to that raw fraction, rather than to a normalized
interpretation of its value. One need not allocate an oversized host integer:
two 64-bit product limbs and a restoring integer quotient routine suffice.
Keep enough exact remainder information to round once on the selected grid.
In particular, do not collapse a product tail to sticky and then treat a left
shift of that sticky bit as an exact product bit.

For PC p, put `U = 2^(64-p)` and express the exact raw fraction as
`U*(q + r/d)`, with integer q and `0 <= r < d`. Increment q for nearest when
`2*r > d`, or on equality when q is odd; for RC down when the result is
negative and r is nonzero; for RC up when positive and r is nonzero; never
for chop. The stored fraction is `(q + increment)*U`.

If that produces `2^64`, J11 replaces it with `8000000000000000` and increases
E by one. The raw `0625` source modifier at `#0303` supplies that integer bit;
it does not by itself construct infinity. This is the same rounding carry
already derived for addition.
[J11 raw words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L300),
[source modifier definition](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L81).

Fresh PE means r was nonzero, even if nearest did not increment. J11's
rounding-decision condition writes C1 to the increment decision. C0, C2 and
C3 are not assigned by these finite paths. Existing sticky flags remain set.
An unmasked precision exception is an after exception: the rounded result
and normal pop still complete. VALID unnormal input alone does not raise DE.
[J11 C1 side effect](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[Intel precision policy, S-19 and S-20](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=702).

## Unnormal significance and the specific unresolved crossing

The literal finish has at most one left shift. Far below the integer bit,
it therefore retains leading zeros, preserving reduced significance instead
of behaving like a canonical binary80 backend. Multiplication can round a
nonzero exact product to a zero fraction while the computed exponent remains
ordinary. The fraction-only write and subsequent exponent-only write retain
VALID, producing a pseudozero. This is not an underflow solely because the
fraction became zero. Intel also explicitly describes pseudozeros arising
from multiplication of small unnormals.
[Original pseudozero description and common path](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

There is nevertheless a real source conflict to preserve:

- An unnormal multiplication input guarantees `P < 2^127`, but does not
  guarantee `P < 2^126`. For `P >= 2^126`, the literal one-bit shift sets the
  integer bit and makes the unrounded result normal.
- An unnormal dividend guarantees `S_A < S_B` for a normal divisor, but
  `2*S_A` can be at least S_B. The literal shift then makes the unrounded
  quotient normal.

Those cases conflict with the broad Intel unnormal-result rule. The
condition-table label `33 = reg bit63` is provisional and not an exhaustive
control specification. Possible explanations include a fuller predicate,
mode-dependent setup, or a simplified manual rule. No reviewed source here
chooses between them. Do not invent an input-unnormal override merely to make
the table and the shorthand agree.

### Does the Intel prose allow the crossing?

The scan was checked visually, including the preceding S-69 prose. The
multiplication row says, "If either operand is unnormal, result is unnormal."
The unnormal-dividend division row says, "Result is unnormal." Neither row
has a footnote or a gained-significance qualification. S-69 describes a
general exception when an unnormal has an insignificant effect on a
calculation with a normal. Its concrete example is addition of a small
unnormal to a large normal. It does not authorize normalizing an unnormal
dividend merely because dividing its fraction by a normal brings a leading
one within one shift.

Thus the table reads as an unnormal-result requirement. Describing it as
merely permitting normal output when significance is gained would add an
exception that the source does not state. Conversely, that wording does not
identify a hidden normalization predicate in the chip. The source conflict
must remain explicit.
[Visually checked Intel S-69](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=752),
[S-70](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753).

### What the other condition33 paths establish

The same six-bit condition number is used in several routines, rather than
being a multiplication-only instruction selector:

| Path | Relevant raw sequence | Cross-path consequence |
| --- | --- | --- |
| J1 finish | `#0380 c1e6`, then `#0381 3172`, `#0382 1422`, `#0383 7c02` | A false test takes one left shift and expAdj decrement. |
| J3 finish | `#0422 1620`, `#0423 c1e6`, then the same shift/transfer/decrement | The tested datum is the transferred quotient. |
| Short/long real underflow store | `#0507` J11, `#0508 1014`, `#0509 1b2e`, `#0510 d1e6` | The branch selects external exponent one when a rounded fraction reaches the minimum normal boundary, otherwise exponent zero. |
| Integer store boundary | `#0572 d466` branches back to the sign/boundary handling at `#0549` | A magnitude bit test is needed at the signed-integer limit. |
| FPTAN final normalization | `#1119 0e20`, `#1120 c1e6`, then left shift at `#1122` | A fraction is loaded into the sum and its integer bit selects the final scaling. |

The store path is especially useful: `#0509` changes tmpC after the rounded
fraction is stored. A model that assigns condition33 from the raw source of
the most recent transfer would therefore be wrong. The retained sum
integer-bit interpretation fits this sequence and the explicit quotient
transfer before J3's test; the sequence does not identify every latch or idle
bus drive in the physical circuit.
A general OR with an input-unnormal indicator would make the store branch
incorrect for allowed unnormal inputs that remain denormal after conversion.
No evidence supports introducing such a general OR.
[Raw store paths](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L500),
[first-hand FPTAN decode](https://www.righto.com/2026/09/8087-tangent-cordic.html).

The first-hand condition report classifies conditions `30` through `37` as
datapath conditions and labels `33` as register bit63. Its photographed
four-input multiplexer example traces conditions `38` through `3b`, not
condition33's input circuitry. The report explicitly calls the table
provisional. These sources support the retained sum/quotient bit63 reading;
they do not independently establish every mode-dependent input to that bit
test.
[Condition classification and circuit example](https://www.righto.com/2025/12/8087-microcode-conditions.html).

### Initialization and implementation decision

The hardware initialization of expAdj to one is described directly by the
first-hand addition report. In J1 and J3, the visible updates before exponent
construction are the optional decrement and J11's rounding-carry increment.
The fixed modes `6948` and `6a48` and fixed full loop counts introduce no
visible operand-dependent normalization branch. Raw `f40c` and J3's `e400`
remain incompletely decoded; no reviewed source says that either changes
fraction alignment or expAdj specifically for an unnormal. Exact normal
calibration confirms the exponent equations, but does not prove such an
unnormal-specific action absent. Do not invent one as a repair.

The implementation conclusion is to retain the literal one-optional-shift
algorithm as the current reconstruction and to keep its contradictory
unnormal crossing cases explicitly identified. It is sufficient to implement
and independently test the normal paths and the far-unnormal reconstruction
without waiting for physical hardware. A strict claim covering the crossing
requires resolving condition33 or identifying a mode-dependent setup action
in a source. The table alone does not determine whether the no-shift
alternative is correct for other unnormal values.

The exact alternative is easy to express and useful for comparison. Omitting
the optional shift for every unnormal multiplication input would use delta
one; omitting it for an unnormal dividend would use division delta zero.
That preserves unnormality, but changes raw alignment even in cases where
both alternatives remain unnormal. It must not silently replace the literal
decode. An implementation can keep the crossing cases explicitly guarded
while adding the source-supported far-unnormal reconstruction and recording
this interpretation in its tests. The lack of physical hardware does not
prevent such a source-based implementation; it limits the independence of
the evidence.

The unresolved numerical controls are narrower than an unspecified complete
nanomachine: condition33's full predicate and any mode-dependent fraction or
expAdj initialization are the points to resolve. The shortened multiplication
selector and individual divider nanosteps also remain relevant to timing.

## Literal tests and discriminators

Use VALID tags, legal PCs, all RC modes, all sign combinations and all TOP
values. The following rows use E `3fff` for both inputs, PC64 and nearest
unless specified. Except where marked, results follow the literal decoded
alignment above. Every row remains an expectation derived from sources.

| Operation, raw fractions | Result `E:S` | Flags and purpose |
| --- | --- | --- |
| `8000000000000000 * 8000000000000000` | `3fff:8000000000000000` | Exact normal product, PE zero, C1 zero. |
| `c000000000000000 * c000000000000000` | `4000:9000000000000000` | Product needs the larger exponent, no initial left shift. |
| `0000000080000000 * 0000000100000000` | `3fff:0000000000000001` | Product P is `2^63`; far-unnormal exact grid result. |
| `0000000000000001 * 0000000000000001` | `3fff:0000000000000000`, VALID | Nonzero product rounds to pseudozero, PE one, C1 zero. |
| Previous row, RC up | `3fff:0000000000000001` | PE one, C1 one on the raw grid. |
| `6000000000000000 * c000000000000000` | Literal `3fff:9000000000000000`; no-shift alternative `4000:4800000000000000` | Crossing conflict: same numeric value, different normalization. |
| `8000000000000000 / 8000000000000000` | `3fff:8000000000000000` | Exact normal quotient. |
| `8000000000000000 / c000000000000000` | `3ffe:aaaaaaaaaaaaaaab` | Normal quotient needs one shift; PE one, C1 one. |
| `0000000000000001 / 8000000000000000` | `3ffe:0000000000000002` | Far-unnormal exact quotient, PE zero, C1 zero. |
| `0000000000000001 / 8000000000000001` | `3ffe:0000000000000002` | Inexact quotient, PE one, C1 one. |
| `4000000000000000 / 8000000000000000` | Literal `3ffe:8000000000000000`; no-shift alternative `3fff:4000000000000000` | Division crossing conflict and exact identity discriminator. |

The exact P=`2^63` multiplication row particularly distinguishes the two
alignment interpretations: suppressing the shift instead gives
`4000:0000000000000000`, VALID, with PE set. For the far-unnormal division by
one row, suppressing the shift returns `3fff:0000000000000001`. Numeric-only
comparisons would miss the latter difference entirely.

Extend product tests across P boundaries `2^126` and `2^127`, raw rounding
half-way points for PC24 and PC53, and carry from all-ones fractions. Extend
division tests across `S_A = S_B` and `2*S_A = S_B`, exact and recurring
quotients, and remainders one bit below/at/above each rounding half-way point.
For output pseudozeros, compare tag, stored exponent, sign, PE and C1, not
just numeric zero. Keep range cases in a separate comparison set until J16
processing for these alignments is implemented.
