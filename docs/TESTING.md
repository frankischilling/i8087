# Validation report

Initial validation ran on 2026-10-20 in a Linux x86_64 container. Later Windows
and hosted updates appear below. These are software tests, not original-8087
hardware measurements.

## Initial build configurations

| Configuration | Result |
| --- | --- |
| GCC, C11, CMake Release, warnings as errors | All six CTest suites passed |
| Clang, C11, Debug, AddressSanitizer + UndefinedBehaviorSanitizer, warnings as errors | All six suites passed; no sanitizer errors observed |
| GNU binutils, 8086-mode DOS probe assembly | CAPTURE.COM assembled to 1,196 bytes; not executed under DOS or on hardware |
| Clang static analyzer on library and replay sources | No diagnostics emitted; not a proof of correctness |
| Tests-disabled CMake build | Library, replay and demo built with GCC warnings as errors |
| Standalone C demo | `(2 + 3) * 4 = 20`; control 03bf, status 0000, tags ffff |

Compiler warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
The initial Linux run did not test the minimum supported Python interpreter,
Windows/MSVC or non-x86 hosts. Later hosted and Windows results are recorded below.

## Initial test output

```text
PASS: 12717 instruction/API assertions (including all 2048 ESC/ModR/M pairs)
PASS: 557507 original-8087 regression assertions (document-derived, not hardware captures)
PASS: 50000 deterministic raw-state cases, 100000 duplicate executions; safety invariants only
PASS: 26048 independent exact-rational arithmetic, sqrt, conversion and rounding cases
PASS: 19000 independent Fraction instruction-level remainder/range cases
PASS: 12 trace-tool scenarios; 388 model-only round trips; no hardware evidence
100% tests passed, 0 tests failed out of 6
```

Do not add these numbers together and call the sum hardware test vectors.
Assertions can inspect multiple aspects of one case, safety checks are not a
numerical oracle, and model-only round trips are not independent hardware tests.

## What the suites check

**Instruction/API tests.** Operand order, memory/register forms, pops, tags,
stack wrap, environment transfers, initialization, BCD and integer conversions,
original infinity behavior, controls, callback failure and known-gap guards.
The all-encoding traversal is a decoder/safety smoke test, not an independent
proof of each encoding's original behavior. The instruction suite checks
supported paths and guards; broader original-specific checks are in the
regression suite.

**Original-specific regressions.** Raw/signed-zero FXTRACT across control modes;
FPREM quotient history and sign combinations; zero-tag FSCALE, raw unnormal
exponent adjustment; tiny FPTAN; copied tags and pseudozeros; before/after
exceptions; pending-event/mask/WAIT behavior; exponent wrapping and conversion
range exceptions; NaN sign selection and rollback. The large assertion count is
mainly enumeration of integer FPREM cases and their previous condition bits.
The expectations come from documentation/analysis, not physical captures.

As a regression check, the new C regression executable was also linked against
the earlier library. It failed its first signed-zero FXTRACT check, while
the revised library passed. This demonstrates detection of a real change in
behavior, not that all revised behavior is silicon-verified.

**Raw-state safety.** Seed `0x8087198020260200`; 50,000 generated states, each
executed twice on isolated instances/memories. Checks repeatability, rollback,
callback failure, bounded transfers and state-field invariants across normal,
special, malformed and inconsistent tag/value inputs. Not an accuracy oracle.
Observed first-execution outcomes: 17,963 OK; 10,677 pending; 8,689 guarded;
11,015 bad opcode; 1,656 memory fault; zero bad arguments.

**Numerical backend oracle.** Python Fraction and integer square root, not host
floating point. Seed `0x80871980`; 14,000 arithmetic/square-root cases, 12,000
conversion/integer-rounding cases and 48 directed ties. Checks canonical results,
flags and rounding direction. This backend oracle does not encode every original
8087 exception policy; the instruction layer adds those policies.

**Instruction-level Fraction oracle.** Seed `0x80872026`; 6,500 fractional FPREM
cases, 10,000 arithmetic cases around register range boundaries and 2,500
masked underflow/borderline cases. All execute actual encoded instructions via
C replay. Expected results come from independent rational arithmetic and
unbounded rounding, with the documented range-exponent adjustment. This checks
the implementation of the stated model, not the validity of every silicon
assumption behind that model.

**Trace protocol.** Malformed/truncated/trailing inputs, count limits, opcode
rejection, MODEL-vs-HARDWARE markers, explicit self-test handling and retention
of research-gap rows. The 388 round trips compare model output with itself and
are deliberately labeled as such. The DOS collector's real runtime behavior is
not covered by these host-tool tests.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-Werror
cmake --build build
ctest --test-dir build -V

cmake -S . -B build-sanitize -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DI8087_SANITIZE=ON -DCMAKE_C_FLAGS=-Werror
cmake --build build-sanitize
ctest --test-dir build-sanitize -V

sh tools/hardware/build.sh
```

## C formatting

The repository's `.clang-format` defines the C style: K&R braces and a blank
line between function and other definition blocks. Apply it to tracked
sources and headers from the source root:

```sh
git ls-files -z -- '*.c' '*.h' | xargs -0 clang-format -i
git ls-files -z -- '*.c' '*.h' | xargs -0 clang-format --dry-run --Werror
```

## Not established

No complete 1:1 equivalence, full original transcendental outputs, arbitrary
unnormal arithmetic, constant-ROM loading, all exceptional/undefined raw results,
original-hardware behavior, DOS collector runtime, Windows/ARM portability,
cycle timing or electrical-interface behavior has been established. See
ACCURACY.md and HARDWARE_VALIDATION.md for remaining work and capture scope.

## Hosted and Windows checks after import

[PR #2](https://github.com/frankischilling/i8087/pull/2) adds CI and fixes MSVC
build diagnostics. All four jobs passed on its final head, commit
`750656885b9bb08bae351c98efd49633649bbd42`:
[Actions run 39509538139](https://github.com/frankischilling/i8087/actions/runs/39509538139).
The jobs cover all six suites with GCC/Linux, Clang/Linux with ASan/UBSan, and
MSVC/Windows, plus a tests-disabled build, demo and reproducible DOS collector
assembly. Logs are retained as run artifacts.

Local Windows runs also passed all six suites with MinGW GCC and MSVC, using
Python and CMake. Warnings were errors; the replay and numerical test tools use
a target-local Microsoft CRT setting to
retain ISO C stdio. No original-hardware execution or non-x86 portability was
established by these runs. The preceding Linux results remain historical records
of the imported package.

## Replay input integrity

The trace suite includes checks that use disposable files
to verify that path aliases, hard links and existing outputs are rejected
without overwriting data. Truncated and trailing reference payloads must fail
before output creation; a complete synthetic framing fixture still supplies
its instruction address to a successful replay. These fixtures test file
handling and contain no captured hardware results.

The input-alias regression was run against the imported tool and failed with
`replay overwrote its input through a path alias`. The corrected tool passes
that test. This is evidence of the file-handling correction, not processor
accuracy.

## FRNDINT raw-state regressions

The instruction regression suite adds 4,608 FRNDINT cases with literal expected
encodings. These cover raw unnormals, pseudozeros, denormals, signed zeros,
halfway fractions and integral boundaries across signs, all PC/RC fields,
three stack positions, and four DE/PE mask combinations. Tests distinguish
before-operation DE rollback from a committed rounded result with an unmasked
PE, while checking tags, untouched registers and preservation of sticky flags.

The test follows the original Intel normalization rule and leaves the core's
normalization order intact. [The source investigation](research/FRNDINT.md)
records the manual and recovered-microcode evidence and its limits. A separate
384-input masked suite is ready for the current hardware collector. Its
generation and model replay are tooling checks; no hardware output is supplied.
As a sensitivity check, a temporary copy of the backend was changed to bypass
normalization based on the stored exponent. The new encoding check failed;
the unchanged core passed. The temporary mutation is not part of the library.

## Campaign and report preservation

Disposable fixtures check that JSON reports cannot overwrite inputs,
synthetic captures, model output, hard links or earlier reports. Campaign
generation must reject existing directories before replacing any input or
metadata, including a partially populated directory and an empty directory.
Fresh destinations still work. Both preservation checks failed against the
overwriting tools and pass with exclusive report creation and directory
reservation. These checks protect files; their synthetic data is not hardware
evidence.

## Masked directed overflow

The source-derived instruction tests cover full-width register saturation,
directed PE/OE policy, sticky flags, unmasked PE continuation, register/pop
forms and arithmetic memory operands. Short/long real stores separately check
their widths, committed stores/pops and unmasked overflow suppression. Nearest
rounding remains a control case. The new register encoding check fails against
the core before the correction and passes afterward.

The instruction-level Fraction oracle adds 2,000 seeded directed-overflow
cases to its previous 19,000, calculating exact results and then applying the
stated original policy. It also checks result tags. The capture generator adds
96 masked overflow inputs; its tooling check retains 24 unresolved CHOP rows as
blocked. All of these expectations are document/die-derived or model-only.
They do not establish agreement with a physical 8087. See
[the investigation](research/DIRECTED_OVERFLOW.md) for the source discrepancy
and limits of the decoded-path inference.

## Constant-load campaign

The input builder supports arbitrary TOP while keeping saved values in logical
ST order and tags in physical order. Three independently stated tag words
check that rotation. A model-only campaign exercises 2,688 masked constant-load
inputs: all seven opcodes, documented PC fields, all RC fields, every TOP,
empty/full stacks, and initially clear/set PE.

The 384 supported FLD1/FLDZ rows check values, stack position, tags, sticky flags
and preserved raw register storage. The 2,304 remaining rows must retain the
constant-ROM or stack-fault diagnostic and leave all architectural bytes
unchanged. Those rejections are counted as blocked. The campaign supplies no
expected hardware bytes for the five unresolved constant payloads. See
[the source investigation](research/CONSTANT_LOADS.md).

## F2XM1 signed-zero return

The encoded-instruction regressions add 2,048 canonical signed-zero cases across
PC/RC fields, TOP, exception masks, sticky flags and empty/full surrounding
stacks. They check raw storage, tags, control and the absence of a new exception
or interrupt. Pseudozeros and inconsistent tags retain their guards. The first
new instruction check fails before the correction and passes afterward.

The smoke capture campaign adds 24 masked signed-zero inputs covering documented
PC fields and all rounding settings. These are inputs, not hardware outputs.
[The source investigation](research/F2XM1_ZERO.md) records the precise early
return and the unverified condition-bit, general-operand and timing behavior.

## Original alternate encodings

The regression suite exercises all 72 Intel-documented alternate register
forms at every TOP. Its 442,368 cases cover every PC/RC field, both gap policies,
IE/DE masks, empty registers, mismatched tags, NaN payloads, denormals, signed
zeros and sticky flags. Literal normal-state expectations check destinations,
tags, stack effects and comparison flags; exceptional cases also compare
against the corresponding existing operation. Each committed form must save
its actual opcode. Adjacent reserved/later encodings remain rejected.

The FFREE-plus-pop tests independently require both tag writes before the TOP
increment, with every raw register payload preserved. The first new acceptance
check failed against the rejecting decoder and passes after the correction.
See [the manual and microcode evidence](research/ALTERNATE_ENCODINGS.md).

A separate 72-input masked campaign checks generation and model replay. A
synthetic flag change must produce 24 comparison mismatches in the default
mask view. The collector binary is rebuilt from the updated permitted-opcode
list and its instruction stream inspected. It has not been executed under DOS
or on an original 8087; none of these tests supplies physical evidence.

All six suites passed locally with GCC and MSVC on Windows and Clang with
ASan/UBSan under Linux for this change. Hosted checks are still required
before merging the pull request.

## Bounded unnormal-plus-zero arithmetic

The instruction suite adds 774,144 register cases across nine add/subtract
forms, all destination indices, TOP, operand order, both signs, RC, masks,
sticky flags and both gap policies. Literal raw expectations distinguish
preserved unnormal storage from a normalized value. Another 192 cases check
zero memory operands in short/long real and integer formats. Denormal memory
loads retain their masked guard or unmasked before-operation DE response.
Negative cases retain guards for reserved PC, pseudozeros, inconsistent
tags and excluded exponent edges.

The first new acceptance check failed before the correction. The model-only
capture campaign includes 3,072 PC64 masked register inputs and checks raw result,
tags, C1 and untouched storage. None of these files contains original execution
results. [The investigation](research/UNNORMAL_ZERO_ARITHMETIC.md) explains the
bounded J2 alignment, normalization and precision path.

The PC24/53 extension adds 2,064,384 register cases and 6,144 memory-zero cases.
Sixteen literal input/result rows distinguish raw alignment from canonical
normalization, covering exact values, both ties, neighboring residues, carry
into the integer bit and zero-rounded fractions. The matrix crosses opcode
direction, destinations, TOP, signs, rounding, masks, sticky flags and policies.
It requires the original exponent and VALID tag even for pseudozero output,
PE for discarded bits and C1 for a magnitude increment. An unmasked PE commits
the result and pop, sets IR/BUSY and asserts the interrupt when enabled.

The first new acceptance check fails against the previous guard. The full
`unnormal-zero` campaign now has 15,360 masked inputs. Model replay expectations
use Python Fraction on the signed raw rounding grid, independently of the C
bit operations. They check every saved register, tag, status bit and guard.
The [reduced-precision derivation](research/UNNORMAL_REDUCED_PRECISION.md) records
why these expectations differ from canonical arithmetic. No physical capture
has established them as original-chip measurements.

## Original finite-add datapath

The separate C subsystem uses stored-exponent alignment, fraction/GRS add and
subtract, the decoded normalization gate, half-bit promotion and raw PC
rounding. Canonical backend operations retain their independent mathematical
tests. Integration currently requires properly tagged nonzero finite inputs
with stored E from `0041` through `7ffd`, or a canonical zero, and at least
one unnormal. Exact cancellation and canonical both-zero early returns also
accept the full finite exponent range, including normal cancellation. These
paths preserve C1, independently of zero sign and tag. Fresh DE, reserved PC
and unnormal range completion remain guarded. Intel's original GRS definition
supplies the tail
for all alignment distances, including the 64/65 boundaries and sticky-only
counts from 66 onward.

The `original_add_datapath` suite crosses 47 literal cases through 1,516,032
register executions, with all nine operation/destination/pop forms, indices,
TOP, both operand orders, signs, masks, sticky flags, initial C1 and policies.
Another 6,912 executions check short/long real and integer nonzero memory
operands. Private-path controls check half-bit promotion, range rejection and
the cancellation payload and C1-preserve action. The matrix includes exponent
edges, same-E cancellation and equal numeric values with different stored E.
A further 9,216 both-zero executions cover effective signs and early returns.
The first acceptance check fails when linked to the earlier instruction core.
A separate normal-cancellation check reproduces C1 clearing in that core and
passes with the source-derived preservation rule.

The `original_add_fraction_oracle` suite checks 30,000 seeded inputs by choosing
the source-prescribed alignment and rounding exact signed Python Fractions.
It uses no C bit-operation implementation or host floating point to calculate
the result. It also replays all 46,080 finite-add capture inputs, checking
46,080 supported raw outcomes, including all 6,912 exact-cancellation inputs.
These expectations exercise the reconstruction; they do not independently
prove the physical chip follows it. CI requires both finite-add suites along
with the remainder suites and the six existing suites.

[The source investigation](research/FINITE_ADD_SUBTRACT.md) distinguishes
resolved arithmetic branches from remaining range completion. The
random cases include distances through 32,700. No original-hardware output has
been supplied for this work.

The FPREM regression crosses 98,304 cases with stored distances of 64, 65
and 66 but numerically small unnormal dividends. It checks a zero partial
result with C2 still set and the source-derived short-chunk history. Both
policies produce the same raw state without an approximation marker. PC/RC,
TOP, old quotient flags, both signs, old C2 and sticky flags are crossed.
The acceptance check fails against the earlier strict partial guard.

## Original remainder datapath

The separate C subsystem computes `divmod(S_A * 2^k, S_B)` with
`k = min(stored exponent distance, 63)` for a nonnegative distance. It uses a
64-bit remainder and a separate carry for each shift, without host floating
point or a compiler-specific wide integer. A negative distance bypasses division
and normalizes the dividend. The result keeps the dividend sign and C2 follows
the stored long-count branch even if the fraction becomes zero. PC and RC do
not shorten the remainder. Exact tiny results use a full-width masked
denormal or the original unmasked exponent adjustment.

The `original_remainder_datapath` suite crosses 14 literal cases through
917,504 executions: both signs, all PC/RC fields, TOP, old quotient history,
old C2, sticky flags, masks and both policies. It checks every raw register,
tags, full status/control, saved instruction and opcode, interrupt state and
guard reporting. Another 4,096 sequences perform five successive reductions
through completion, checking 20,480 intermediate states. The first acceptance test fails against
the earlier instruction core, which guarded every normal partial reduction.

Six paired literal cases cross completed D=63 against partial distances
64, 65, 127 and 128, totaling 491,520 executions. Fractions, dividend exponent
and old history stay the same while the divisor exponent changes. Cases
cover chunks Q=0, 1, 2 and 3, zero and nonzero remainders and pseudozero inputs.
The expected remainder and H update are the same across each pair; C2 differs.
[The divider-control derivation](research/FPREM_DIVIDER_CONTROLS.md) audits
the actual path dependencies. Partial C0/C1/C3 remain a source-derived
inference for bits Intel labels undefined, rather than a measured chip result.

Five tiny-result literals cross 327,680 executions with normal and unnormal
dividends, a negative stored distance, a partial result, signs, PC/RC, TOP,
old history, sticky flags, exception masks, global interrupt masking and both
policies. Masked results are full-width SPECIAL-tag denormals; unmasked
results keep the normalized fraction with the original exponent adjustment.
Both raise UE without fresh PE, preserve other registers and complete quotient
flags. The first check fails against the earlier tiny-result guard.
[The exact-grid proof](research/FPREM_TINY_RESULTS.md) explains why the
denormalizing shift cannot discard a nonzero bit.

The `original_remainder_integer_oracle` suite checks 30,000 seeded ordinary
cases, 12,000 exact tiny-result cases and 128 complete sequences, totaling
55,316 input states in the recorded run. Python
integer `divmod` and `bit_length` determine each result independently of the
C serial divider. Exact Fractions separately check the final mathematical
remainder. Every input is replayed with strict and opt-in policies; neither
uses an approximation on this supported path. The test compares raw payloads,
tags, every status bit, control and unchanged sentinel registers. Long
sequences cover stored distances through 32,700 and assert at least a 63-place
reduction for every nonzero partial result.

All ten suites passed locally with GCC and MSVC on Windows and Clang with
ASan/UBSan under Linux. The direct C demo, formatting check and diff check also
passed. CI requires all ten suites. Hosted checks must pass on the final pull
request head. Physical hardware is optional corroboration and is not a prerequisite
for completing source-backed work.
