# F2XM1 on canonical signed zero

Research date: 2026-10-20. This note covers canonical +0 and -0 in a register
tagged ZERO. It does not validate the general F2XM1 algorithm or supply a
physical execution trace.

## Finding

Intel's zero-result table and the recovered microcode support a narrow
unguarded path for canonical signed zero with the correct ZERO tag:

- Preserve the raw sign/exponent and significand bytes.
- Preserve the ZERO tag, TOP and all stack contents.
- Raise no new precision or underflow exception.
- Produce the same result for every documented PC and RC setting.

Normal instruction bookkeeping still applies. This is not a claim that the
entire saved environment remains byte-identical, or that the operation takes
zero clock cycles.

## Original Intel evidence

The 1981 supplement, printed S-71, Table S-26, explicitly gives +0 for F2XM1
with +0 and -0 for F2XM1 with -0. The scan was visually checked at PDF page
754. Unlike some arithmetic zero entries in the same table, these entries do
not attach a rounding-mode sign rule or an underflow qualification.
[Intel Table S-26](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=754).

This is a documented original-8087 signed-zero result. It should not be inferred
from the mathematical expression `2^0 - 1`, which alone does not settle the
negative-zero encoding or exception state.

## Recovered zero branch

Addresses are decimal. The inspected granite snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

| Address | Decoded operation |
| --- | --- |
| `#0900` | Copy ST(0), including its tag, to `tmpA`. |
| `#0901` | Select constant index 7 for the nonzero calculation. |
| `#0902` | Jump to `#0923` if condition `0x20` is true. |
| `#0923` | Read next instruction, ending F2XM1. |

The zero branch performs no stack push/pop and no write to ST(0). It preserves
the original register payload and tag through the absence of a write.
[Recovered F2XM1 listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L901).

Condition `0x20` is a **ZERO-tag test**, not a raw significand-zero test.
Shirriff's condition table distinguishes this from other tests of temporary
register bits. The table warns that its decoding remains under development.
[Die-level condition decoding](https://www.righto.com/2025/12/8087-microcode-conditions.html).

The fixed `f004` signal at `#0905` occurs after the zero branch, so zero skips
it. The older generated listing calls this signal overflow, but newer die
analysis identifies the same encoding at `#0346` as precision. Avoid copying
the older exception labels into the specification. Zero also skips the
rounding/division work and the J16 range-response call at `#0921`. Thus there
is no executed precision or underflow response on this branch.
[Newer exception decoding](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

No PC or RC test is executed before returning. Constant selection at `#0901`
does not load that constant into a stack register. These observations explain
why the zero path can be implemented without the general transcendental
algorithm or an authenticated constant-ROM payload.

## Required scope restriction

A conservative fast path should require all of:

```text
ST(0) tag == ZERO
ST(0).significand == 0
(ST(0).sign_exponent & 0x7fff) == 0
```

The sign bit may be either value and must survive. A numeric helper that calls
every zero-significand encoding zero would also accept pseudo-zeros. Those are
finite nonzero-exponent encodings normally tagged VALID and do not satisfy the
observed ZERO-tag branch. Keep them guarded. Also keep empty, SPECIAL-tagged,
or otherwise inconsistent tag/payload inputs outside this proven subset.

The decoded branch suggests that a fabricated ZERO tag could cause other raw
payloads to bypass the calculation too, but that is a separate hardware test
question. Do not broaden the canonical-zero implementation on that inference.

## Discriminating capture cases

Execute `D9 F0` on each canonical input:

| Input | Sign/exponent | Significand | ST(0) tag | Expected payload/tag |
| --- | --- | --- | --- | --- |
| +0 | `0x0000` | `0x0000000000000000` | ZERO | unchanged |
| -0 | `0x8000` | `0x0000000000000000` | ZERO | unchanged |

For each sign, use TOP=t for t=0 through 7, and
`CW = 0x00bf | (PC << 8) | (RC << 10)`, with PC=0,2,3 and RC=0,1,2,3.
These are 192 base cases, all exceptions masked. Clear IR/BUSY and exception
flags initially. With only ST(0) occupied, the physical tag word is
`0xffff & ~(2 << (2*t))`, for example `0xfffd` at TOP=0 and `0x7fff` at
TOP=7. TOP, tag word and all raw register bytes should remain unchanged;
initially clear PE and UE should remain clear.

Use distinct raw storage patterns under the other empty tags to detect an
accidental push, pop or data rewrite. Repeat with all other registers occupied:
their VALID tags and ST(0)'s ZERO tag give tag word `1 << (2*t)`. A zero path
that pushes an extra result would fail those full-stack cases.

Repeat selected cases with initially sticky PE, UE and PE|UE to check
preservation separately from new exceptions. A dedicated fixture can also
unmask PE and UE while their initial flags are clear; the current hardware
collector intentionally accepts only all-masked inputs. Reserved PC=1 and
inconsistent tags belong in separate research campaigns.

Condition-code bits, exact cycle counts, BUSY transitions and internal latch
effects remain unverified by this investigation. Record them without treating
implementation-selected values as captured truth. The result and flag
expectations above are document/die-derived until a specimen produces the raw
state records.
