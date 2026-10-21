# Original 8087 partial remainder

Research date: 2026-10-20. This traces the original FPREM entry and its shared
hardware divider. It distinguishes documented behavior, consequences of the
recovered control flow, and a candidate integer reconstruction. It is not a
physical 8087 capture. The [divider-control follow-up](FPREM_DIVIDER_CONTROLS.md)
establishes the 63-place reduction cap through an independent normalization
path. The implementation follows that source-derived count and Intel's
completed-history rule. Short unnormal partial history and tiny-result
completion still retain guards; the sections below record the raw controls
and distinguish those remaining limits.

## Sources and scope

The FPREM description starts on printed S-34, not S-32 or S-33, and continues
on S-35. These are PDF ordinals 717 and 718, or zero-based indices 716 and 717,
in the full Intel manual. Intel specifies exact remainder, dividend sign,
software iteration using C2, and reduction of a magnitude difference of up to
`2^64` in one execution. S-70 says an unnormal dividend produces a normalized
remainder and an unnormal divisor raises invalid operation.
[Intel S-34](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=717),
[S-35](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=718),
[S-70](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753).

Intel's processor patent describes non-restoring division: the previous
remainder sign selects addition or subtraction, and its inverse enters the
quotient shift register. Modulus uses the same machinery with an
exponent-difference-dependent cycle count. Its example computes `10 mod 8`
with one same-exponent subtraction. The patent omits the microcode program
and does not identify the recovered divider-control words.
[Intel patent US4338675A, arithmetic sections B and C](https://patents.google.com/patent/US4338675A/en).

The published die analysis confirms that division's inner loop is hardware.
The decoder identifies FPREM's entry as decimal `#1586`. The raw snapshot used
here is commit `745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.
[First-hand adder analysis](https://www.righto.com/2026/06/intel-8087-adder-reverse-engineered.html),
[entrypoint decode](https://www.righto.com/2026/02/8087-instruction-decoding.html),
[raw FPREM words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L1587).

The finite analysis below assumes nonzero VALID inputs and a normalized
divisor. Keep denormal preprocessing, pseudozero inputs, invalid divisors and
tiny-result exception completion separate. Safe-range captures can use both
stored exponents at least `0041` so that a subsequent normalization of up to
63 places cannot underflow.

## Entry, exponent selection and C2

`#1586`, raw `61ce`, clears C2. Its interpretation is corroborated by the same
word at compare/test `#0814` and FXAM `#1471`, where the later `60ce` sets C2.
`#1587` loads ST(0) into tmpA. The TOP increment, load and decrement at
`#1588` through `#1590` obtain ST(1) in tmpB without a final stack pop. Normal
completion writes only ST(0) at `#1646`.

The ordinary VALID path reaches this sequence without prenormalizing tmpA:

| Address | Raw word | Operation or consequence |
| --- | --- | --- |
| `#1606` | `0c0e` | tmpB exponent to B register with rounding storage. |
| `#1607` | `481a` | Subtract stored exponents, `D = E_A - E_B`, carry-in one. |
| `#1608` | `1002` | D to exponent converter. |
| `#1609` | `dbe1` | Negative D takes the dividend-copy route at `#1601`. |
| `#1610` | `f408` | Clear internal flag. |
| `#1611` | `c0bd` | Skip the following set when D is nonzero. |
| `#1612` | `f008` | Set internal flag for D zero. This is not PE. |
| `#1613` | `100c` | D to B register. |
| `#1614` | `c1b4` | If exponent-converter bits 6 through 14 are zero, use D. |
| `#1615` | `1f00` | Constant-source transfer to shift-count register. |
| `#1616` | `60ce` | Set C2 on this long-count route. |
| `#1617` | `000c` | Shift-count register to B register. |
| `#1618` | `480a` | Subtract chosen count k from E_A. |
| `#1619` | `1010` | Save `E_base = E_A - k` into tmpA exponent. |
| `#1620` | `4600` | Zero plus B register, copying k into the sum. |
| `#1621` | `1b0c` | Bottom-bit constant one to B register. |
| `#1622` | `500a` | Subtract one, giving k minus one. |
| `#1623` | `102a` | Load this value into the loop counter. |

The newer addition report establishes the carry-in distinction: operation
code 5 is subtraction with carry-in one; operation code 4 omits carry-in.
The generated listing's older `DEC` and exception names must not be taken
literally. In particular, `#1620` does not double k, and `f008` is the internal
flag operation rather than a precision exception.
[Adder operation definitions](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html),
[updated exception/flag interpretation](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

**C2 follows the selected path.** For VALID inputs, the long-count branch is
chosen from the stored exponent difference, not from exponents obtained by
normalizing an unnormal dividend first. C2 is set before reduction. There is
no subsequent clear in `#1618` through `#1647`, including the zero-result
route. Thus a chosen long reduction can leave C2 set even when its result
becomes zero or smaller than the original divisor. This is a consequence of
the recovered control flow, pending physical validation.

## Divider invocation and count derivation

`#1624` supplies tmpA fraction to the signed sum register, `#1625` supplies
tmpB fraction to the B register, and `#1626` selects mode with raw `6b48`.
The zero-D flag skips `#1628`; otherwise raw `52c0` starts an arithmetic
operation in the divider mode. `#1629` issues raw `f40c`, whose specific
action is not decoded here. Raw `524b` at `#1630` runs the shared division
sequence, followed by raw `5244` at
`#1631`. These last controls are not a fully decoded executable nanomachine
specification in the public notes.

The functional reconstruction is `k = min(D, 63)`, followed by k-plus-one binary
quotient trials, with the first leading zero trial treated specially. That
would make the capped case a reduction using up to 64 quotient bits. It fits
the loop-counter construction and the original manual's upper bound. It is
not proved solely by those observations.

The raw decode has these limits, separated from the functional reconstruction:

- **`1f00` count source.** Source `1f` supplies all ones on the exponent bus
  and `c000000000000000` on the fraction bus when forming real indefinite.
  The disassembler's register names do not settle its physical bus selector.
  The follow-up resolves the functional count through `#1326`/`#1327`, which
  pairs the same count source with an explicit 63-place normalization shift.
  Reading low fraction bits and choosing zero contradicts that independent
  use. This establishes the count without claiming an electrical bus-tap decode.
- **Initial trial and loop termination.** The behavior of `52c0`, `f40c`,
  `524b` and `5244` in mode `6b48`, including final non-restoring correction, is needed
  to reproduce the individual internal transitions. The original patent's
  same-exponent modulus example and the raw exponent/count relationship
  support the exact integer reconstruction independently of later processors.
  Those controls remain relevant to cycle timing and hidden partial history.
- **Unnormal divisor rejection.** Intel explicitly requires invalid
  operation, but there is no visible direct tmpB integer-bit branch. The
  complete entry anomaly predicate `23`, divider initialization and other
  implicit controls must be checked before locating that detection. The
  older generated name "stack overflow" does not describe the full predicate.

The first-hand source documents the indefinite bus drivers. The recovered
notes identify the count and quotient registers, but leave relevant BC and
divider operations incomplete.
[Bus-driver analysis](https://www.righto.com/2026/05/microcode-inside-intel-8087-floating.html),
[register and BC notes](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md#L17).

## Result normalization, sign and rounding

`#1632` tests the sum fraction. A zero sum saves the dividend sign at `#1633`,
loads whole zero at `#1634`, and restores that sign at `#1635`. This produces
canonical signed zero, including a ZERO tag, rather than a retained-exponent
pseudozero.

A nonzero sum calls J21 at `#1637`. J21 uses the leading-zero byte and bit
counts to produce the normalized left-shifter output and the total shift n
in the B register. `#1638` stores the fraction and `#1639` subtracts n from
the saved exponent. `#1640` and `#1641` update the exponent converter and
tmpA exponent. Thus the outer exponent relation is:

```text
E_result = E_A - k - n
```

On the negative-D bypass, k is zero. The normalization is unconditional for
a nonzero sum, including a VALID unnormal dividend. There is no J2-style
significance gate. The tmpA sign is retained for a nonzero result. The
nonzero tail changes fraction and exponent separately, preserving its VALID
tag. A range exception invokes J16 at `#1642`.
[J21 and the normalization tail](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L802).

The finite safe-range path never calls J11. Its exact remainder is not
quantized to PC24 or PC53 and has no ordinary RC rounding decision. Intel's
no-precision-exception rule agrees with that structure. Tiny-result handling
through J16 remains separate; this note does not resolve the control that
prevents an inappropriate precision exception during that completion.

## Quotient condition codes and history

The final transfer sequence is explicit: `#1643` shifts quotient-register
output right by three, `#1644` transfers the shifter result into the sum with
rounding storage, and raw `6a06` at `#1645` writes the three quotient condition
bits. The notes map the low selected bits to C1, C3 and C0, in that order.
This path also executes after the zero-result and negative-D bypasses. It
does not preserve arbitrary old C0/C1/C3 merely because the result is partial.

Intel's original Table 4b gives the completed small-quotient history. Define
`H = 4*C0 + 2*C3 + C1` and let Q be the magnitude of the generated quotient:

```text
Q < 2:       H_new = ((H_old << 1) | Q) & 7
2 <= Q < 4:  H_new = ((H_old << 2) | Q) & 7
Q >= 4:      H_new = Q & 7
```

Table 4a marks C0/C1/C3 undefined for an incomplete reduction. That is a limit
of the documented interface, not proof that hardware leaves them unchanged.
[Intel datasheet Tables 4a and 4b, printed 3-99, PDF ordinal 10, index 9](https://www.ardent-tool.com/CPU/docs/Intel/808x/8087/datasheets/205835-007.pdf#page=10).

The physical connection that seeds quotient history, the initial-trial
suppression and treatment of many leading zero quotient trials are still
missing. This especially affects VALID unnormal dividends: stored D can be
large while the numeric quotient is 0, 1 or 2. A software update based only
on `bit_length(Q)` must be checked against that raw path. The existing
completed-remainder tests do not independently prove this hidden shifter
behavior. Also record each partial step's flags rather than assuming the
last completed step reports the low bits of the full mathematical quotient.

## Candidate integer reconstruction

The follow-up establishes k and applies Intel Table 4b to completed history.
An exact finite reconstruction can use a wide unsigned remainder and avoid
floating point:

```text
If D < 0:
    R = S_A; k = 0; Q = 0
Otherwise:
    select k using the original count circuit
    Q, R = divmod(S_A << k, S_B)
    C2 = (the original long-count branch was selected)
If R is zero:
    return canonical zero with dividend sign
Otherwise:
    n = leading_zeros_64(R)
    return VALID(sign_A, E_A - k - n, R << n)
Apply original quotient-shifter history, not a later x87 rule
```

For a normalized divisor, the division remainder R is below S_B and fits 64
bits. A wide temporary or a bit-serial integer routine can avoid an oversized
host shift. This is an exact arithmetic representation of the candidate
reduction, not evidence for the unresolved count circuit. The current
backend's fixed 32-trial partial step should remain explicitly approximate
until the original count is proved; successful final modulo results do not
validate intermediate states.

## Literal discriminating captures

Use raw `E:S`, both nonzero tags VALID, all eight old H patterns, both initial
C2 values, all legal PC/RC settings and every TOP. Save state after every
FPREM until C2 clears, including after a zero partial result. ST(1), TOP and
unrelated registers must also be recorded.

| Dividend | Divisor | Purpose and bounded expectation |
| --- | --- | --- |
| `4002:a000000000000000` | `4002:8000000000000000` | `10 mod 8 = 2`, result `4000:8000000000000000`; one generated quotient bit. |
| `4000:4000000000000000` | `4000:8000000000000000` | Unnormal dividend 1 below modulus 2; normalize to `3fff:8000000000000000`, quotient zero. |
| `3fff:8000000000000000` | `4000:8000000000000000` | Same numeric pair as previous row, but negative stored D; compares loop and bypass histories. |
| `4040:0000000000000001` | `3fff:8000000000000000` | Numeric `4 mod 1`, but stored D 65; tests long selection before input normalization. |
| `4001:8000000000000000` | `3fff:8000000000000000` | Same numeric `4 mod 1` with normal encoding; compare C2 and quotient history. |
| `403f:8000000000000001` | `3fff:c000000000000000` | D 64, distinguishes candidate 64-bit and existing 32-trial reduction. |
| `4040:8000000000000001` | `3fff:c000000000000000` | D 65, checks whether the long count changes with D. |
| `407f:8000000000000001` | `3fff:c000000000000000` | D 128, checks repeated long steps and sequence flags. |
| `403f:8000000000000001` | `3fff:8000000000000001` | A long-step zero candidate; C2 should remain set if the chosen reduction reaches zero. |

For the D 64 row with divisor fraction `c000000000000000`, conditional on
`k=63` and the candidate integer reduction, the first result is canonical
positive zero with C2 set. A `k=31` reduction instead returns
`401f:8000000100000000`. These are computed discriminators, not measured
8087 expectations. The selected chunk quotients have low bits 6 and 5,
respectively; partial condition-code observations would test their physical
mapping rather than relying on their architecturally undefined designation.

Also capture stored D values 0, 1, 2, 31, 32, 62, 63, 64, 65, 66, 127 and
128 with fractions just below/equal/above the divisor fraction. Use unnormal
dividends with 1, 2, 31, 32 and 63 leading zeros, and same-value normal
encodings. Repeat the table with each operand sign reversed independently.
Separate IE-mask tests for unnormal divisors and range-mask tests for tiny
remainders are required before calling this a complete finite datapath.

The next evidence needed is a divider-control decode or attributable
original-chip captures covering `1f00`, mode `6b48`, the three nanomachine
operations and quotient-history initialization. Neither a later x87 result
nor an integer modulo oracle resolves those controls.

## Implementation checkpoint

The instruction layer now selects the guarded long path from the stored
exponent difference for VALID finite inputs. A normalized fallback that
produces zero still leaves C2 set and remains labeled as partial. Strict mode
does not commit its candidate result. This corrects the earlier acceptance
of a completed zero after input normalization changed the path selection.

The regression matrix checks that distinction across signs, PC/RC, TOP, old
condition codes, sticky flags and both policies. It does not treat a guard
pass as proof of the divider algorithm. The numerical partial reduction and
its quotient history remain unfinished work in the existing arithmetic issue.
Physical access is not required: resolve the controls through original online
sources, sound derivation and discriminating software reference tests, using
published captures as optional corroboration.
