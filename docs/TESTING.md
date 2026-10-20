# Validation report

Run on 2026-10-20 in a Linux x86_64 container. These are software tests, not
original-8087 hardware measurements.

## Build configurations actually exercised

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

## Actual test output

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
