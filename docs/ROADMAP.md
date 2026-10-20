# Accuracy work

The target is an original Intel 8087 on an 8088/8087 PC-style system. The
provisional system baseline is an IBM 5150 configuration. A silicon stepping
has not been established. [Hardware evidence](research/HARDWARE_EVIDENCE.md)
records the sources searched and the limits of the available evidence.

These issues track the known gaps. Their acceptance criteria require more
than passing the current mathematical and regression tests.

| Work | Issue | Completion evidence |
| --- | --- | --- |
| Identify a specimen/revision and validate captures | [#3](https://github.com/frankischilling/i8087/issues/3) | Attributable raw states, collector validation and reproducible comparisons |
| Model execution and the 8088 bus by cycle | [#4](https://github.com/frankischilling/i8087/issues/4) | Original clock, bus and pin traces, including overlap and exceptions |
| Reproduce transcendental datapaths | [#5](https://github.com/frankischilling/i8087/issues/5) | Original algorithms, internal rounding and raw result comparisons |
| Preserve unnormal significance and partial FPREM steps | [#6](https://github.com/frankischilling/i8087/issues/6) | Intermediate state and condition-code comparisons |
| Resolve range and precision behavior | [#7](https://github.com/frankischilling/i8087/issues/7) | Discriminating tests for conflicting sources and range boundaries |
| Validate exception continuation and special results | [#8](https://github.com/frankischilling/i8087/issues/8) | Handler/sequence fixtures, stack cases and exact special-result bytes |
| Recover constant loading | [#9](https://github.com/frankischilling/i8087/issues/9) | ROM/loading derivation and results across control modes |
| Audit decoding, transfers and reserved state | [#10](https://github.com/frankischilling/i8087/issues/10) | Independent opcode matrix and memory/noncanonical-state captures |

Start with reproducible failures supported by original documentation, while
developing the capture and timing fixtures. Keep each correction small enough
to review and test. Add a focused issue when a concrete bug is found within a
broader area; closing that bug does not close the area's hardware-validation
requirement.

Every accuracy change needs a stated source, the behavior it predicts, and
tests that distinguish the correction from the existing behavior. Record
whether an expectation comes from mathematics, documentation, die analysis or
a physical measurement. Keep disagreements visible until evidence resolves
them. A later x87 result is useful only when its relevance to the 8087 has
been established.

The existing opt-in approximate paths remain identified in
[ACCURACY.md](ACCURACY.md). New work must not silently treat an approximation,
a blocked case or a model-only comparison as an original-hardware pass.
CI protects regressions; it does not certify 1:1 accuracy.
