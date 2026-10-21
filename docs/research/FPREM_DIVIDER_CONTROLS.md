# FPREM divider controls and an integer reconstruction

## Result of this investigation

The recovered program supports a functional reduction with
`k = min(E_A - E_B, 63)` when the stored exponent difference is nonnegative.
The count of 63 has an independent normalization-path cross-check. An exact
integer implementation can produce the remainder without reproducing every
nanomachine control signal. This is a source-derived reconstruction, not a
silicon capture or a cycle-count derivation.

Intel directly specifies completed quotient history by dividend range. A
paired-path comparison transfers that rule to partial reductions, including
unnormal dividends whose generated quotient is zero, one, two or three. Both
paths run the same fraction divider with count 63. The differences in the
exponent converter, general shift count and C2 do not supply an identified
input to quotient generation or extraction. This supports an implementable
history rule without requiring a reconstruction of every quotient latch.
Partial C0/C1/C3 remain architecturally undefined, so distinguish this
source-derived hardware inference from Intel's completed-result contract.

## Why the count is 63

Intel's shifter patent describes separate three-bit byte and bit controls,
together selecting 0 through 63 places. Its broader datapath diagram permits
count input from several sources, including a transfer associated with the
fraction bus. The diagram is not a decode of the packaged chip's register-zero
write enable or its individual input taps.
[Intel US4509144A, figures 3 and 8](https://patents.google.com/patent/US4509144A/en).

First-hand die analysis likewise finds six count-select multiplexers and two
three-to-eight decoders. Division uses specialized sum/quotient shifting rather
than the general barrel shifter. These are separate mechanisms; a six-bit
barrel-shift control alone does not prove how often the divider iterates.
[Ken Shirriff, shifter control and note 8](https://www.righto.com/2020/05/die-analysis-of-8087-math-coprocessors.html).

Source `1f` drives exponent/sign ones and fraction `c000000000000000` when
creating indefinite. Reading its low fraction bits would therefore give zero,
while reading six exponent bits gives 63. This is why interpreting the source
name as a literal NaN and taking its significand's low bits is unsafe.
[First-hand source-driver description](https://www.righto.com/2026/05/microcode-inside-intel-8087-floating.html).

The useful control-flow cross-check is the other occurrence of raw `1f00`:

| Address | Raw word | Relationship |
| --- | --- | --- |
| `#1324` | `1722` | Transfer tmpC to the sum register. |
| `#1325` | `c23d` | Nonzero path goes to ordinary leading-zero normalization. |
| `#1326` | `1f00` | Zero/cancellation fallback selects the special count. |
| `#1327` | `373f` | Explicitly shift tmpC left by 63. |
| `#1328` | `000e` | Read that selected count into B for exponent correction. |
| `#1330` | `aafc` | Other path calls ordinary normalization. |
| `#1334` | `480a` | Apply the selected normalization amount to the exponent. |

Both paths join at `#1333`. A fallback count of zero or 48 would disagree with
the explicitly selected 63-place shift on the same path. The consistent
functional interpretation of `1f00` is therefore count 63. This cross-check
does not require choosing a particular physical bus tap.
[Recovered words, pinned snapshot](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1325).

FPREM uses that identical word at `#1615`, after testing whether stored
`D = E_A - E_B` fits six bits. Short counts retain D; long counts select 63 and
set C2. The result exponent starts at `E_A - k`. Together, the branch, the
independent 63-place normalization path and the result-exponent subtraction
justify `k = min(D, 63)` as the functional reconstruction. An exact physical
input-tap claim remains stronger than this result.
[FPREM count path](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1607).

## Quotient trials and exact arithmetic

Intel's original arithmetic patent describes non-restoring division with the
inverse remainder sign entering the quotient register. Modulus uses that
datapath for an exponent-dependent count. Its same-exponent example,
`10 mod 8`, performs one trial subtraction and returns 2. It explicitly permits
an equivalent two's-complement non-restoring algorithm.
[Intel US4338675A, arithmetic sections B and C](https://patents.google.com/patent/US4338675A/en).

The recovered FPREM sequence loads `k - 1` into the loop counter, loads the two
fractions, selects mode `6b48`, conditionally executes `52c0`, then executes
`f40c`, `524b` and `5244`. The D-zero flag controls the extra initialization
operation. The code distinguishes the same-exponent single-trial case from
the shifted repeated-trial case. It does not support treating the counter
field as an ordinary unsigned C loop bound in isolation.
[Divider invocation](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1621).

The arithmetic observable is determined without assigning an invented name to
each of those controls. With unsigned significands A and B, B normalized and
nonzero, the quotient and remainder are:

```text
Q, R = divmod(A * 2^k, B)
```

This has k-plus-one aligned binary trial positions, from weight `2^k` down to
one. It needs one same-position trial followed by k shift-and-trial steps.
Here is an equivalent restoring formulation; its branch structure is a
software implementation choice, not a claim that the chip restores after
every subtraction:

```text
R = A
Q = 0
trial:
    bit = (R >= B)
    if bit: R -= B
    Q = (Q << 1) | bit
repeat k times:
    R = 2 * R
    perform trial
```

After every trial, `0 <= R < B`. A shift needs at most 65 bits, so a carry bit
plus a 64-bit unsigned remainder suffices in portable C. Preserve only three
quotient bits when the full quotient is unnecessary. The initial trial can
be zero; it is still an arithmetic position and must not be confused with a
condition-code history shift.

If R is nonzero, count its leading zeros n and return normalized fraction
`R << n` at exponent `E_A - k - n`, retaining the dividend sign. If R is zero,
return canonical zero with the dividend sign. Negative stored D bypasses
division: normalize A without changing its numeric value and use Q zero.
These exponent and sign relationships agree with the recovered normalization
tail at `#1632` through `#1642`.
[Normalization tail](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1633).

C2 records the long-count branch. Do not clear it because the resulting R is
zero or already below the original divisor. A subsequent invocation takes the
completion path. Repeated nonzero partial results reduce stored exponent
distance by at least 63; normalization can reduce it further. This provides
a termination invariant independent of host floating point.

## Completed quotient history, including unnormal dividends

Intel Table 4b states the completed condition-code rule by dividend value:
below two moduli, below four moduli, or at least four moduli. It does not add
a normalized-dividend restriction. Let `H = 4*C0 + 2*C3 + C1`, and let Q be the
magnitude quotient for this completed instruction:

```text
Q < 2:       H_new = ((H_old << 1) | Q) & 7
2 <= Q < 4:  H_new = ((H_old << 2) | Q) & 7
Q >= 4:      H_new = Q & 7
```

The table is sufficient to implement these architectural results without
proving the quotient register's initial electrical contents. Raw unnormal
dividends are especially useful tests because stored exponent distance and
numeric quotient length can differ greatly.
[Intel datasheet, Tables 4a and 4b, printed 3-99](https://kib.kiev.ua/x86docs/Intel/486/205835-007.pdf#page=10).

For example, VALID dividend `403e:0000000000000001` and normal divisor
`3fff:8000000000000000` both represent one. Stored D is 63, but Q is one and
the exact remainder is zero. With old H equal to five, Table 4b specifies new
H equal to three. Shifting history once for each of the 64 arithmetic trial
positions would instead erase the history. This discriminates functional
quotient history from the raw divider's arithmetic iteration count.

## Partial quotient history from paired paths

Compare two finite, correctly tagged VALID inputs with the same stored E_A,
fractions A and B, signs, control word and old H. B is normalized and nonzero.
Choose E_B so the first instruction has D equal to 63, and the second has D
at least 64. Keeping E_A fixed also makes their result exponent bookkeeping
equal. The first instruction is completed and therefore has Table 4b's
explicit history rule. The second takes the capped partial path.
For this comparison, hold the other preinstruction state fixed as well;
old H is not an assertion that the entire quotient register has been decoded.
Table 4b defines the completed output without requiring such a decode.

The relevant state comparison is:

| State or control | Completed D = 63 | Partial D >= 64 | Effect after the paths join |
| --- | --- | --- | --- |
| C2 | Cleared at `#1586`. | Cleared, then set at `#1616`. | No subsequent FPREM branch reads C2. |
| Stored tmpB exponent | E_A minus 63. | E_A minus D. | `#1625` reads only fraction B; the exponent is not read again. |
| Exponent converter | D = 63 at `#1608`. | D >= 64 at `#1608`. | The difference survives into division and needs the separate audit below. |
| Internal D-zero flag | Clear. | Clear. | Both skip `#1612` and execute `#1628`. |
| Reduction amount in B | 63 through `#1613`. | 63 through `#1615` and `#1617`. | Same subtraction at `#1618`. |
| tmpA exponent | E_A minus 63 after `#1619`. | E_A minus 63 after `#1619`. | Same result exponent before normalization. |
| General barrel shift-count register | May retain an old count. | Written with 63 at `#1615`. | The fraction divider uses specialized shifting; later extraction selects immediate 3. |
| Divider loop counter | 62 after `#1623`. | 62 after `#1623`. | Same hardware loop bound. |
| Sum, sign and GRS inputs | Loaded from fraction A at `#1624`. | Same load. | Earlier exponent-arithmetic sums are replaced. |
| B fraction and GRS input | Loaded from fraction B at `#1625`. | Same load. | Earlier count values in B are replaced. |
| Divider controls | `6b48`, `52c0`, `f40c`, `524b`, `5244`. | Identical sequence. | Same fraction calculation and quotient-generation controls. |

The differing prefix has no identified quotient-register write or C0/C1/C3
write. It selects a count, sets C2 and returns that count to B. Its transient
bus values are replaced by the common exponent arithmetic and operand loads.
Thus both paths present the same quotient-relevant initial state and inputs
to the common divider sequence. This is a functional dependency argument;
it does not assert that every internal latch has the same value.
[Count selection through divider invocation](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1607),
[fraction sources, loop counter and BC status writers](https://github.com/shirriff/granite/blob/2f52a129ef0d2dc51be9e874beeab06e3d9b6d6b/tools/8087mc/bin/8087.md#L16).

### Why the surviving exponent difference does not change the quotient

Intel's original division description supplies the dependency needed here:
the sign of the sum selects the next add or subtract, and its inverse enters
the quotient register. Its modulus variation changes the number of cycles.
The recovered program has already converted D into the same capped count
and counter in both cases. The separate exponent converter is not an operand
of that fraction calculation.
[Intel US4338675A, figure 7 and arithmetic sections B and C](https://patents.google.com/patent/US4338675A/en).

This separation also agrees with the die analysis: fraction-bus input and B
feed the adder; the divider accumulates quotient bits in its own register and
uses a hardware inner loop. The general barrel shifter is not the divider's
specialized shifter. Therefore the long prefix's register-zero write is not
an extra divider shift or a second divider loop bound.
[First-hand adder and divider analysis](https://www.righto.com/2026/06/intel-8087-adder-reverse-engineered.html),
[first-hand shifter analysis, note 8](https://www.righto.com/2020/05/die-analysis-of-8087-math-coprocessors.html).

The exponent converter does affect some rounding-bit operations, so it would
be incorrect to discard its difference merely because FPREM is exact. The
first-hand FSCALE analysis identifies that dependency for the special
rounding(H/h) operations. FPREM's common divider block and quotient extraction
do not execute those operations. They load the same fraction/GRS operands,
use the same divider controls, then select the quotient register directly.
[Exponent-dependent rounding operations, note 17 and J20/J11 listings](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

For a nonzero R, J21 computes a normalization count from that same R, replacing
the old general shift count. `#1639` computes the same corrected exponent and
`#1640` overwrites the exponent converter before the range test. For R equal
to zero, `#1633` through `#1636` instead construct the same signed zero and
bypass that overwrite. In that case expConv still differs, but `#1643` uses
an immediate three-place shift of the quotient register. `#1644` transfers
its output to the sum/GRS register, and `#1645` writes bits 0, 1 and 2 to C1,
C3 and C0. This path has no exponent-converter condition or rounding(H/h)
operation. General-count retention therefore does not distinguish the zero
tail either.
[J21 normalization words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L802),
[zero, nonzero and quotient-extraction tails](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1633),
[immediate shift and status-write fields](https://github.com/shirriff/granite/blob/2f52a129ef0d2dc51be9e874beeab06e3d9b6d6b/tools/8087mc/bin/8087.md#L132).

The original fraction-bus patent independently places quotient assembly in
the fraction datapath, with the inverted sum sign entering the quotient
shift register. Its exponent control determines numerical scale separately.
This supplies a concrete circuit boundary for the equivalence, rather than
an assumption that unspecified logic has no effect.
[Intel US4484259A, figure 7 and division description](https://patents.google.com/patent/US4484259A/en).

### Implementable rule and evidence limit

Apply the same three-way H update given above to the generated chunk
`Q = floor(A * 2^63 / B)` on the partial path, while keeping C2 set. In
particular, Q zero or one shifts old H once, and Q two or three shifts it
twice. Leading zero arithmetic positions do not independently erase old H.
For Q at least four, the chunk's low three bits replace it.

This follows by transferring Intel's completed D=63 contract across the same
fraction-divider and extraction path. It is stronger than choosing a
convenient deterministic value for undefined bits. It remains a source-derived
inference for partial C0/C1/C3 because Intel Table 4a deliberately leaves them
undefined. A contradictory capture or an identified exponent/C2 input to the
divider or quotient write enable would require revising the rule. None of
the inspected control words, original datapath descriptions or first-hand
decode supplies such a dependency. A full transistor netlist or a physical
capture is not needed to implement this supported reconstruction, and this
argument does not establish electrical behavior or divider timing.

Keep exceptional input paths and tiny-result range completion outside this
equivalence. On the in-range paired paths, both instructions take the same
zero or normalization tail. The long prefix executes extra microinstructions,
so equal fraction behavior also does not imply equal instruction timing.

### Literal paired cases

Use VALID dividend exponent `4040` and normalized divisor fraction
`c000000000000000`. Divisor exponent `4001` gives completed D=63; changing
only that exponent to `4000`, `3fff`, `3fc1` or `3fc0` gives partial D=64,
65, 127 or 128. For every row below the paired result payload is the
same, apart from the retained dividend sign; C2 is zero for the completed
case and one for the partial cases.

| Dividend fraction A | Chunk Q | Integer R | Normalized result | H with old H = 5 |
| --- | --- | --- | --- | --- |
| `0000000000000000` | 0 | 0 | Signed canonical zero. | 2 |
| `0000000000000001` | 0 | `8000000000000000` | `4001:8000000000000000` | 2 |
| `0000000000000002` | 1 | `4000000000000000` | `4000:8000000000000000` | 3 |
| `0000000000000003` | 2 | 0 | Signed canonical zero. | 6 |
| `0000000000000004` | 2 | `8000000000000000` | `4001:8000000000000000` | 6 |
| `0000000000000005` | 3 | `4000000000000000` | `4000:8000000000000000` | 7 |

The first row is a VALID pseudozero, not a ZERO-tag shortcut. Repeat these
pairs for every old H, both signs of each operand, both initial C2 values and
all precision/rounding controls. The completed member tests Intel's Table 4b;
the partial member tests the source-derived equivalence. Integer divmod
alone checks neither member's history behavior against hardware.

## Software checks for the reconstruction

A separate Python experiment exhaustively checked the serial formulation
against integer `divmod(A << k, B)` at widths two through eight. It included
every nonzero A, every normalized B, and D from zero through twice the width,
with a width-minus-one cap. All 709,770 cases agreed and preserved the
remainder bound. This verifies the arithmetic transformation; it does not
validate the inferred cap or undocumented latch behavior against silicon.

Implementation tests should keep literal count-boundary cases at D 0, 1, 62,
63, 64, 65 and 128, both signs, raw unnormal dividends, all old H values and
both initial C2 values. Compare every intermediate normalized remainder and
C2 against the capped integer model, then test completed history directly
against Table 4b and partial history against the paired D=63 cases. Keep
tiny-result exception completion and invalid divisors separate from this
finite normalized-divisor derivation.

## Implementation checkpoint

`src/original_remainder.c` implements this finite reconstruction with a separate
carry bit and unsigned 64-bit remainder. It normalizes the result after every
step and reports C2 from stored D. The instruction layer accepts correctly
tagged finite VALID dividends, including unnormals and pseudozeros, and normal
finite divisors. Completed history uses Table 4b. Partial history follows the
same H update through the paired-path derivation above, including chunks with
fewer than three significant quotient bits. Both policies complete this
source-derived path without a partial-remainder guard.

The private result retains a signed normalized exponent for
[tiny-result completion](FPREM_TINY_RESULTS.md). Masked output is an exact
full-width denormal; unmasked output uses the original exponent adjustment.
Both raise UE without fresh PE and complete quotient flags. Zero, exceptional
and unsupported input handling stays in the instruction layer. This is a numerical/state
implementation; it does not implement the divider's cycle timing.
[Validation and independent references](../TESTING.md#original-remainder-datapath)
cover literal states, exact integer comparisons and complete reduction sequences.

An input pseudozero is VALID and follows the unnormal path, including stored
count selection even with a zero fraction. Its result is canonical signed
zero, and a long selected path still sets C2. Intel's pseudozero rules treat
it as unnormal for this operation; no zero-tag shortcut applies. The literal
matrix checks completed and long-count pseudozeros. The long-count
acceptance check fails against the earlier canonical-zero bypass.
[Intel pseudozero rules, S-72](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=755).
