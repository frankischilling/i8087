# Changes

## Unreleased

- Use the requested C formatting with K&R braces and blank lines between
  function and other definition blocks.
- Preserve bounded PC64 unnormal-plus-zero add/subtract results without
  normalizing their stored exponent and significand. Keep other unnormal
  cases guarded and provide discriminating capture inputs.
- Extend that bounded path to PC24/53 using the stored significand alignment.
  Preserve exponent and VALID tag through rounding to a pseudozero or into the
  integer bit, with PE/C1 and unmasked precision continuation tests.
- Add GCC/Linux, Clang ASan/UBSan and MSVC/Windows CI, with required checks on
  `main`. Fix MSVC build diagnostics while retaining portable C stdio.
- Preserve replay inputs and captures through exclusive output creation;
  reject incomplete or trailing reference payloads before output creation.
- Require new JSON report files and campaign directories to preserve inputs,
  captures, prior reports and provenance across repeated or aliased paths.
- Test original FRNDINT normalization and exception stages with literal raw
  expectations; provide masked inputs for future hardware collection.
- Add a constant-load capture campaign across controls and stack positions.
  Keep unresolved payloads guarded and count blocked model rows separately.
- Accept all 72 alternate register encodings explicitly listed in Intel
  Table A-2, including the two tag writes of FFREE-plus-pop. Align replay,
  capture inputs, comparison masks and the unverified DOS collector.
- Apply Intel's masked directed-overflow flag rule in the instruction layer
  and construct the normal finite register bound at full width after PC
  adjustment. Keep memory formats width-limited and preserve prior sticky OE.
- Record the provisional 8088/8087 target, capture search and unresolved
  source discrepancies under `docs/research`. No original-hardware validation
  or cycle-accuracy claim has been established.

## Imported implementation, 2026-10-20

This is an implementation and validation update, not a 1:1 certification.
The public C API remains source-compatible with the earlier implementation.
Internal `sf_result` now carries unbounded rounding information and is not a
public ABI.

### Instruction corrections

- FXTRACT decomposes raw fields instead of first normalizing the operand.
  Canonical signed zero yields two zeros with the original sign. Finite
  unnormal and pseudozero field extraction retains the significand.
- Complete FPREM updates the original quotient condition-code shift history
  for quotients below four. It no longer requires approximate mode for every
  ordinary finite remainder. Partial reduction remains guarded.
- FSCALE checks zero tags before examining either operand; in-range scaling
  adjusts only the exponent, preserving an unnormal's missing significance.
  Nonzero scale magnitudes below one are rejected as an undefined original
  domain rather than silently given a later-x87 result.
- FPTAN has the die-derived zero/tiny-positive-normal bypass: return the
  original angle plus a pushed one; signal precision for nonzero input.
  The general CORDIC algorithm has NOT been implemented.
- Register transfers and sign changes preserve tags instead of reconstructing
  them from the value. Pseudozeros classify as unnormal/valid, not zero.
- Subtraction selects NaN payloads before negating an arithmetic operand,
  avoiding a spurious sign change in the selected NaN.

### Exception and range corrections

- Sticky exception flags and the interrupt-event latch are separate. Changing
  masks alone does not synthesize or clear an already recorded event.
- WAIT is distinct from no-WAIT ESC execution. A pending event does not by
  itself reject every subsequent no-WAIT numeric instruction.
- Unmasked precision is an after-operation exception: rounded register results,
  stores and pops are retained. Before-operation IE/DE/ZE suppress results.
- Unmasked canonical register range exceptions use an unbounded rounded
  significand and an exponent adjustment of plus/minus 24,576. They do not
  reconstruct a wrapped value from an already rounded infinity or subnormal.
- Exact tiny results also report original-style underflow. Unmasked real-store
  range exceptions suppress the store, pop and subsequent precision stage.
- Comparing nonzero unnormals reports a denormal-operand exception. An
  unmasked DE abort takes precedence over later results and their guards.
- Memory write failures continue to roll back the entire instruction state,
  including after-operation exception state, subject to the callback contract.

### Validation tools

- Original-specific regression assertions and a second independent Python
  Fraction oracle operating through actual encoded instructions.
- Deterministic raw-state stress checks for rollback, callback failure,
  instance isolation, bounds and repeatability.
- Versioned input, model-output and hardware-capture formats with distinct
  provenance markers; malformed/truncated input is rejected.
- A standalone 8086 DOS collector, input generation, C replay and a comparator
  that does not count guarded or approximate cases as validation passes.
- The collector was assembled and inspected, not executed on physical hardware.

### Still open

Full transcendental microcode, unnormal arithmetic propagation, partial FPREM,
constant-ROM loading details, several exceptional result/stack paths, malformed
encodings, undocumented aliases, and the bus/queue/timing model remain open.
See ACCURACY.md for the exact qualification, rather than interpreting this list
of fixes as a claim that all remaining encodings are hardware-exact.
