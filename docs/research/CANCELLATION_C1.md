# C1 after exact addition and subtraction cancellation

## Implementation rule

Preserve incoming C1 on a correctly tagged finite J2 exact-cancellation return.
That is the most direct reconstruction of the recovered control flow and
identified C1 writers. It includes normal inputs, VALID unnormal inputs,
canonical zero and retained-exponent pseudozero results, all rounding modes,
operand signs and register destinations. An input exception that prevents J2
from executing is outside this scope. It does not mean that an ordinary exact
result finishing through J11 preserves C1:
J11 evaluates the rounding predicate and writes C1 even when no increment is
needed.

This rule is a source-derived inference, not a physical measurement. It does
not depend on assuming that every undisclosed circuit is inert. The writer
audit and independent invariants below identify what would have to differ
for a clear, sign assignment or other cancellation result to be correct.

## Identified microcode writers

Shirriff's first-hand analysis identifies special circuitry that writes C1
when a control-flow instruction evaluates the rounding-increment condition.
His J11 listing marks both `#0300` and `#0312` as writing C1. The written value
is the increment decision before the control-flow inversion bit is applied:
returning or jumping when the condition is false does not write its inverse.
[Rounding control and C1 side effect](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

A scan of all 1,648 words in the pinned recovered ROM gives:

| Write mechanism | Occurrences | Value written |
| --- | --- | --- |
| Condition `1d` in a control-flow word | `#0300`, `#0312`, `#0588`, `#0729` | Rounding magnitude increment decision. |
| BC control `6b02` | `#1470`, FXAM | tmpA sign through condition `28`. |
| BC control `6a06` | `#1645`, FPREM | Selected quotient bit 0 to C1; bits 1 and 2 to C3 and C0. |

The first two rounding occurrences are J11, the third is decimal store
rounding, and the fourth is FRNDINT. The BC field descriptions distinguish
these status writes from temporary-register sign controls. This is the full
set matching the currently identified microcode C1 write mechanisms, not a
claim that it enumerates all hardwired BIU or reset behavior.
[J11 words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L300),
[FXAM writer](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1471),
[FPREM writer](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L1646),
[BC field notes](https://github.com/shirriff/granite/blob/2f52a129ef0d2dc51be9e874beeab06e3d9b6d6b/tools/8087mc/bin/8087.md#L158).

## Cancellation reaches a different predicate

For nonzero finite operands, J2 tests the fraction at `#0256`, then exposes
the rounding bits at `#0275` and `#0276`. When those bits are also zero, it
takes `#0261`. The zero/pseudozero construction and sign selection finish
through `#0264` to `#0268`, bypassing J11 at `#0290`.

The return at `#0266` is raw `e05b`, selecting condition `2d`, not `1d`.
Condition `2d` selects a directed sign response; J11's `1d` is the
GRS-dependent increment decision. The same `e05b` occurs in directed overflow
at `#0349`. Replacing either condition with the other would change numerical
behavior: cancellation needs negative zero under RC down despite having no
discarded bits. No identified C1-writing BC control occurs in this tail.
[J2 cancellation and J11 paths](https://www.righto.com/2026/10/8087-addition-subtraction-microcode.html),
[raw cancellation words](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L262),
[condition categories and provisional decode limits](https://www.righto.com/2025/12/8087-microcode-conditions.html).

The common arithmetic entry and ordinary completion likewise contain no
identified C1 write. Loading operands, storing tmpA, choosing a destination
and executing the normal pop do not imply executing a rounding condition.
Therefore the closed functional reconstruction reaches cancellation with
old C1 and leaves it unchanged. Clearing it merely because the result is
exact would insert a write on a path that bypasses the identified rounding
writer.
[Common entry and completion](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L2).

The both-ZERO-tag path at `#0232` through `#0235` can also return before J11.
It likewise preserves C1 in this reconstruction. For an actual cancellation
between nonzero values, the ZERO versus VALID output tag changes the payload
construction, not the presence of a C1 writer.
[J2 zero entry](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087mc_out.txt#L233).

## Cross-instruction checks

Intel Table 4b specifies that a completed small-quotient FPREM result retains
old C1 in another quotient condition bit. Consequently a universal
instruction-start C1 clear cannot be correct. Table 4a marks C1 unaffected by
comparison/test instructions, whose decoded paths can also pop the stack.
Thus a universal pop-induced C1 clear cannot explain cancellation either.
[Intel original condition-code tables, printed 3-99](https://kib.kiev.ua/x86docs/Intel/486/205835-007.pdf#page=10).

These constraints do not logically exclude an arithmetic-opcode-specific
initial clear. Such a rule would need positive evidence: a decoded write
enable, an original trace or another source establishing that action. None
was found in the inspected original references and first-hand decode. It
must not be silently added to make the result resemble a later x87.

The separately decoded FXAM sign-to-C1 operation also argues against treating
the temporary sign latch as C1 by default. J2 explicitly copies and toggles
the sign latch while FXAM uses its dedicated status control. A shared-storage
hypothesis would have to explain those distinct controls and the documented
history behavior. It is not supported simply by the word "sign" in a
disassembler label.

The older provisional condition table calls several predicates imprecisely;
for example, later first-hand analysis distinguishes the internal flag from
the precision exception. The preservation conclusion uses actual control
words and the later rounding-writer identification, rather than treating
every old mnemonic as a complete hardware specification.

## Keep C1 writes separate from numeric payloads

A numeric helper should return a C1 action, such as preserve, clear or set,
alongside the payload and exception events. A single `rounded_up = false`
cannot distinguish bypassing the rounding writer from executing it without
incrementing. This distinction also applies to other early returns, which
need their own path audits.

For J2, use:

```text
exact-cancellation tail:           preserve C1
J11 executed, no increment:        clear C1
J11 executed, magnitude increment: set C1
```

Subsequent range handling can execute another rounding operation and write
C1 again. The rule above does not certify unnormal range completion, invalid
operand responses, stack-fault behavior or memory conversion paths. Loading
an environment/status image and hardware/software initialization are separate
hardwired state operations. Their write semantics must not be inferred from
this microcoded finite arithmetic path.

## Discriminating software cases

Seed C1 both clear and set independently of operand signs. Use legal PC/RC
settings, every TOP, both subtraction orders, addition with opposite signs,
and register-pop forms. Include these literal cancellation inputs, shown as
stored exponent and significand:

| Magnitudes before subtraction | Payload expectation | C1 action |
| --- | --- | --- |
| Both `3fff:8000000000000000`, VALID | Canonical zero, ZERO tag. | Preserve. |
| Both `4000:4000000000000000`, VALID | `4000:0000000000000000`, VALID pseudozero. | Preserve. |
| `403e:0000000000000001` and `3fff:8000000000000000`, VALID | Equal numeric magnitudes; larger-stored-E unnormal path retains exponent `403e` and VALID. | Preserve. |

All three produce a negative zero payload only for RC down; this sign is
independent of old C1. There is no new PE on the exact finite cancellation
path. As controls, use a nonzero exact sum that reaches J11 and an inexact
round-up case: they must clear and set C1, respectively, regardless of its
incoming value. Checking only old C1 zero would miss an unconditional-clear
implementation.

These tests establish consistency with the source-derived rule. A modern
x87 comparison cannot independently validate the original hidden C1 behavior.
The remaining stronger claim would be confirmation that the actual original
chip has no additional opcode-qualified write outside the identified control
paths. That is a targeted evidence question, not a requirement to reconstruct
every transistor before implementing this bounded rule.
