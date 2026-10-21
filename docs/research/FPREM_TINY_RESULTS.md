# Original 8087 FPREM tiny-result completion

Research date: 2026-10-20. Original Intel documentation and the recovered J16
and FPREM control flow support exact tiny-result completion without physical
hardware. This note assumes nonzero finite VALID inputs with stored exponents
at least one and a normalized divisor. It does not resolve the separate
partial-reduction count or quotient-history controls.

## Implementation conclusion

A nonzero FPREM result whose normalized biased exponent is below one raises
the original underflow exception even when the result is exactly representable
as a denormal. It raises no fresh precision exception. With UM set, return the
exact full-width denormal, independently of legal PC and RC settings. With UM
clear, return the normalized fraction with its exponent increased by `6000`
and request the underflow interrupt. Both paths then finish the quotient
condition-code transfers and write ST(0).

Intel S-34 says FPREM produces an exact result and does not incur precision.
Table S-32 on S-81 defines arithmetic underflow from the true exponent alone,
below -16382; it adds no inexactness condition. Its masked response specifies
64-bit denormalization and a SPECIAL tag for a nonzero result. This is not a
tiny-and-inexact test imported from a later x87.
[Intel S-34, PDF ordinal 717, index 716](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=717),
[S-81, PDF ordinal 764, index 763](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=764).

## Why denormalization loses no information

Let the exact integer reduction leave nonzero raw remainder R and exponent
base B. Before final normalization, its value is:

```text
R * 2^(B - 16383 - 63)
```

For a nonnegative stored exponent difference `D = E_A - E_B`, a chosen
reduction count `k <= D` gives `B = E_A - k >= E_B >= 1`. For the negative-D
dividend-copy route, `k = 0` and `B = E_A >= 1` instead. These conclusions
require no particular choice of the partial reduction width.

Put `n = clz64(R)`, `S = R << n`, and retain the signed normalized exponent
`E = B - n`. A tiny result has `E <= 0`. The original binary80 denormal grid
has unit `2^(1 - 16383 - 63)`, so the exact denormal significand is:

```text
S_denormal = R << (B - 1)
```

It is an integer because B is at least one. Since R is nonzero, `n <= 63`;
tiny E also implies `n >= B`. Consequently:

```text
-62 <= E <= 0
1 <= 1-E <= 63
0 < S_denormal < 2^63
```

J16 shifts S right by `1-E`. Because `S = R << n` and
`n >= 1-E`, every discarded bit is zero. All GRS bits are therefore zero.
The result cannot round to zero or to the minimum normal number in this
scope. Importantly, this completion never reaches J20's disputed count-at-
least-64 branch.

This is an exact integer derivation from the original representation and
reduction invariants. A low raw remainder may have bits below the PC24 or
PC53 grid; imposing that grid would change an exact FPREM result and
contradict its instruction-specific contract.

## Masked underflow

The recovered snapshot is commit
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`; addresses are decimal.
FPREM normalizes a nonzero R through J21 at `#1637`, stores its fraction and
signed exponent at `#1638` through `#1641`, and calls J16 at `#1642` when that
exponent is outside the normal range.

J16's relevant masked path is:

| Address | Operation |
| --- | --- |
| `#0319` | Update overflow/underflow exception flags from the exponent converter. |
| `#0321` | Select masked completion when no unmasked exception is pending. |
| `#0329` | Select the underflow branch. |
| `#0330` and `#0331` | Compute shift count `1-E`. |
| `#0332` | Call J20 to construct the denormal fraction and GRS. |
| `#0333` and `#0334` | Transfer the fraction with rounding storage and call J11. |
| `#0335` through `#0339` | For the nonzero result, set exponent zero, store fraction, set SPECIAL, return. |

J11's presence does not authorize reducing FPREM to PC24 or PC53. Intel's
exact FPREM contract and S-81's full-width arithmetic-underflow response
determine the required numerical result. The first-hand report describes
the generic routine as adjusting precision, without fully specifying every
instruction/exponent override. A software implementation can directly emit
the proven exact denormal; locating that override in the circuit is useful
corroboration, not a completion gate.
[J16/J20 first-hand decode](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html),
[raw FPREM tail](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1633).

For either sign, masked completion is thus:

```text
result.sign = dividend.sign
result.E = 0
result.S = R << (B - 1)
result.tag = SPECIAL
fresh exceptions = UE
```

Do not add DE because the result is denormal. The denormal-operand event
belongs to a later instruction that consumes it. Preserve any preexisting
sticky exception flags, including PE; no new PE event occurs, even with PM
clear. Original exception status flags are sticky, while internal flags are
cleared for each instruction.
[Intel exception and sticky-flag policy, S-19](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=702).

## Unmasked underflow and continuation

With UM clear, J16 takes `#0322` through `#0328`. The underflow branch adds
`6000` to the signed normalized exponent and stores it, preserving the
normalized S, sign and VALID tag. It returns from the subroutine rather
than terminating the FPREM macro-instruction.

```text
result.sign = dividend.sign
result.E = E + 0x6000       range 5fc2 through 6000 here
result.S = R << n
result.tag = VALID
fresh exceptions = UE
```

Keep the exponent signed through range classification and bias calculation.
Clamping it to the denormal exponent, or classifying its negative encoded
payload as an ordinary positive exponent, loses the required response.
Modular exponent addition is valid only after the underflow direction is known.
Intel S-20 and S-68 explicitly specify an adjusted result in a register and
then an interrupt request; the unchanged-stack rule applies to memory stores,
not FPREM's register destination.
[Intel S-20, PDF ordinal 703, index 702](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=703),
[S-68, PDF ordinal 751, index 750](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=751),
[raw J16 return](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc.txt#L319).

After either J16 return, `#1643` shifts quotient-register output, `#1644`
loads the sum with rounding storage, and `#1645` writes C1/C3/C0 from the
quotient bits. Thus final C1 is the quotient result, not a J11 rounding
indicator. C2 retains the earlier partial/completed choice. `#1646` writes
ST(0), and `#1647` finishes the instruction. TOP and ST(1) remain unchanged.
UM clear requests the interrupt through the original interrupt controls;
IEM can block delivery but does not turn this into a masked numerical result.

A zero exact remainder follows `#1633` through `#1636` instead, constructs
canonical signed zero and bypasses J16. It has no fresh UE. Its quotient
condition codes still complete through the same tail.

## Literal regression cases

Both inputs below are nonzero normal VALID values. Use nearest initially,
then repeat all legal PC values and all RC values. Each row's result and
fresh flags must be identical across those settings. Results are `E:S`.

| Dividend | Divisor | UM set result, SPECIAL unless noted | UM clear result, VALID unless noted | Quotient |
| --- | --- | --- | --- | --- |
| `0001:8000000000000001` | `0001:8000000000000000` | `0000:0000000000000001` | `5fc2:8000000000000000` | 1 |
| `0001:ffffffffffffffff` | `0001:8000000000000000` | `0000:7fffffffffffffff` | `6000:fffffffffffffffe` | 1 |
| `0001:c000000000000001` | `0001:8000000000000000` | `0000:4000000000000001` | `6000:8000000000000002` | 1 |
| `0002:8000000000000001` | `0001:8000000000000000` | `0000:0000000000000002` | `5fc3:8000000000000000` | 2 |
| `0002:c000000000000000` | `0002:8000000000000000` | `0001:8000000000000000`, VALID | Same | 1 |
| `0001:8000000000000000` | `0001:8000000000000000` | `0000:0000000000000000`, ZERO | Same, ZERO | 1 |

The first four rows raise UE and no PE. The final two rows raise neither.
The second and third rows specifically catch PC-induced loss of exact low
bits. Repeat with the dividend sign reversed and require the corresponding
result sign in both mask cases; divisor sign does not change the remainder
sign. Seed all old quotient-condition patterns and require the original
small-quotient history after both masked and unmasked completion.

An independent integer oracle can calculate each exact remainder in units
of the minimum binary80 denormal and compare raw bytes, tag, UE/PE, quotient
flags, pending interrupt and stack state. These tests validate the
source-derived policy without claiming original-hardware comparison.

No reviewed original rule contradicts this bounded derivation. Its important
limits are input denormals or inconsistent tags, the separate partial-step
and quotient-history derivation, and any value produced by an approximate
reduction that does not preserve the exact R/B invariant.

## Implementation checkpoint

The private remainder result retains its normalized fraction and signed E.
The instruction layer uses those fields for exact masked denormalization or
the unmasked exponent adjustment, then applies quotient history and C2. It
records a committed register range result before requesting the interrupt.
The [test report](../TESTING.md#original-remainder-datapath) covers raw payloads,
tags, no fresh PE, all PC/RC fields, masks, sticky flags and interrupt delivery.
