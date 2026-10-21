# FPREM divider controls and an integer reconstruction

## Result of this investigation

The recovered program supports a functional reduction with
`k = min(E_A - E_B, 63)` when the stored exponent difference is nonnegative.
The count of 63 has an independent normalization-path cross-check. An exact
integer implementation can produce the remainder without reproducing every
nanomachine control signal. This is a source-derived reconstruction, not a
silicon capture or a cycle-count derivation.

For completed reductions, Intel directly specifies the quotient condition-code
history by dividend range. That contract also supplies an implementable rule
for a finite unnormal dividend. The physical handling of leading zero quotient
trials is less certain, especially for architecturally undefined partial-step
condition codes. Keep that distinction visible.

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

## Partial condition codes and what is still an inference

The tail always transfers quotient-register output into C1/C3/C0, including
after a partial reduction. Intel Table 4a labels these three bits undefined
when C2 is one. A long reduction with a normal dividend produces at least 63
significant quotient bits, so any old three-bit history has shifted away and
the generated chunk's low bits are the natural reconstruction. For an unnormal
dividend, a long-count path can instead produce Q zero or one. The public
sources above do not independently prove whether every leading zero trial,
only the first, or none before the first one shifts the retained history.

Use the explicit completed rule for completed cases. For partial cases, label
any chosen deterministic C0/C1/C3 reconstruction as an inference and test it
separately from exact R and C2. Do not present a mathematical modulo oracle as
proof of the internal quotient-latch history. Resolving that physical detail
does not require access to a chip; a circuit-level derivation or a stronger
published control decode would also suffice.

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
against Table 4b. Keep tiny-result exception completion and invalid divisors
separate from this finite normalized-divisor derivation.

## Implementation checkpoint

`src/original_remainder.c` implements this finite reconstruction with a separate
carry bit and unsigned 64-bit remainder. It normalizes the result after every
step and reports C2 from stored D. The instruction layer accepts correctly
tagged finite VALID dividends, including unnormals and pseudozeros, and normal finite
divisors. Completed history uses Table 4b. Partial history is accepted when at
least three significant quotient bits replace the old history; shorter partial
quotients remain guarded. The opt-in response then keeps old C0/C1/C3 while
using the original numerical remainder and C2. That deterministic choice is
not evidence for the hidden latch behavior.

Tiny-result range completion remains guarded. Zero, exceptional and unsupported
input handling stays in the instruction layer. This is a numerical/state
implementation; it does not implement the divider's cycle timing.
[Validation and independent references](../TESTING.md#original-remainder-datapath)
cover literal states, exact integer comparisons and complete reduction sequences.

An input pseudozero is VALID and follows the unnormal path, including stored
count selection even with a zero fraction. Its result is canonical signed
zero, and a long selected path still sets C2. Intel's pseudozero rules treat
it as unnormal for this operation; no zero-tag shortcut applies. The literal
matrix checks completed and guarded long-count pseudozeros. The long-count
acceptance check fails against the earlier canonical-zero bypass.
[Intel pseudozero rules, S-72](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=755).
