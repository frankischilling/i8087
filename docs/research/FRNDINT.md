# FRNDINT and raw unnormal encodings

Research date: 2026-10-20. No original-hardware execution capture was available
for this investigation.

## Finding

The evidence supports normalizing an unnormal before the integer-rounding
cutoff test. It does **not** support returning the original raw encoding merely
because its stored exponent is at least `0x403e`.

Consequently, the `sf_round_integer` path that returns `sf_normalize(x)` when
the normalized exponent is at least 63 is consistent with the specific Intel
normalization rule investigated here. This is not a validation of every result,
exception, condition bit or timing path in that function.

## Intel's explicit rule

The 1981 *iAPX 86,88 User's Manual* supplement, printed S-70, Table S-25, gives
the FRNDINT rule for unnormals as: "Normalize as much as possible before
rounding." The scan was visually checked at PDF page 753; this is not an
inference from OCR alone. Printed S-35 describes rounding ST according to RC.
[Intel manual, S-70](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=753)
([instruction description, S-35](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=718)).

Keep these classes separate:

- A normal finite operand has its significand integer bit set.
- A finite unnormal has that bit clear and a nonzero exponent; Intel says it is
  tagged valid and can retain evidence of reduced significance under other
  arithmetic instructions. FRNDINT has the explicit normalization rule above.
- A pseudo-zero has an all-zero significand and a finite nonzero exponent.
  Intel explicitly places FRNDINT among the operations that give it the same
  result as a true zero. S-72 was also visually checked, at PDF page 755.
- A denormal has exponent zero and a nonzero significand. Intel's general
  denormal handling converts it to an equivalent unnormal before continuing
  when the exception is masked.

Sources: [Intel S-69 through S-72](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=752).

## Recovered microcode ordering

Addresses here are decimal. The inspected granite snapshot is
`745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d`.

| Address | Relevant operation in the generated listing |
| --- | --- |
| `#0710` | Copy ST(0) to `tmpA`. |
| `#0720` | Select constant index 6. |
| `#0721` | Call J18 if condition `0x2b` is false. |
| `#0722` through `#0724` | Move the selected constant and `expA` into the comparison calculation. |
| `#0725` | Conditionally branch to `#0736`. |
| `#0736`, `#0737` | Write `tmpA` to ST(0), then finish. |
| `#0779` | J18 entry. |
| `#0780` through `#0784` | Fraction test and a zero-handling return path. |
| `#0787` | Call J21. |
| `#0788` through `#0797` | Adjust exponent/shift amount and write the shifted fraction. |

Thus the J18 call precedes the exponent comparison. The destination at `#0736`
is the working `tmpA`, which J18 can modify, rather than an untouched ST(0)
image. [Recovered FRNDINT listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L711),
[J18 listing](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L780).

Shirriff's die-level condition table identifies `0x2b` as the top significand
bit of `tmpA`. With that interpretation, `#0721` invokes J18 for an unnormal's
clear integer bit. The table itself warns that its interpretations remain
under development. [Condition decoding](https://www.righto.com/2025/12/8087-microcode-conditions.html).

The older generated listing's condition names need care. It labels `0x30` as
`[0x10]<0`; Shirriff's newer addition analysis labels the same condition at
`#0239` as `adder pos`. Do not use the former text to establish FRNDINT's
comparison polarity. [Addition microcode analysis](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html).

This investigation has not independently decoded constant index 6's numeric
value or every adder carry/sign predicate. It therefore does not establish the
exact cutoff equality directly from the ROM. The normalization-before-cutoff
ordering is supported independently by Intel's explicit rule and the call's
position. The [recovered interpretation notes](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md)
still mark unresolved operations and register meanings.

For denormals, Shirriff separately decodes J12 at `#1572` through `#1580`:
it raises the denormal exception, stops if that exception interrupts, and
otherwise changes the exponent to 1 and the tag to valid. FRNDINT calls J12
at `#0712` for the special-tag path. Treat denormals as that separate path,
including mask handling, rather than treating every clear integer bit as the
same input class. [SPECIAL_VAL analysis](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

## Useful regression and capture cases

The following are numerical/specification-derived expectations, not captured
hardware outputs. Encodings use a positive sign; test negative signs and all
rounding modes separately.

| Input biased exponent | Input significand | Value | Normalized integral result |
| --- | --- | --- | --- |
| `0x403e` | `0x0000000000000001` | 1 | exponent `0x3fff`, significand `0x8000000000000000` |
| `0x4040` | `0x4000000000000000` | 2^64 | exponent `0x403f`, significand `0x8000000000000000` |
| `0x4040` | `0x0000000000000000` | pseudo-zero | true zero |

The integral threshold for an already normalized 64-bit significand is an
unbiased exponent of 63 (`0x3fff + 63 = 0x403e`). An unnormal's stored exponent
does not identify its normalized exponent. For example, the first row requires
63 left shifts and a corresponding exponent reduction. A raw stored-exponent
bypass would retain that unnormal encoding despite the documented FRNDINT
normalization rule.

Add a fractional unnormal immediately below that example, a minimum-exponent
unnormal that cannot normalize fully, and an exponent-zero denormal. Preserve
input tags in the fixtures. Compare output bytes, tags and exception state
separately. No finding here justifies treating later x87 behavior, model output
or reconstructed microcode execution as a physical 8087 trace.
