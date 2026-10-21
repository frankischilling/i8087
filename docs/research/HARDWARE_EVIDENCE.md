# Original 8087 hardware evidence and target selection

Research date: 2026-10-20. This note records a bounded source search, not a
claim that all surviving 8087 captures have been found.

## Current conclusion

The initial system target is an **8088/8087 PC-style system**, as requested by
the project owner. An IBM 5150 configuration is a useful concrete starting
point. The silicon stepping remains **unknown**. No attributable raw 8087
execution-state or bus-capture dataset was located in the sources inspected
below. The repository's collector has not produced original-hardware evidence;
its limits remain documented in [HARDWARE_VALIDATION.md](../HARDWARE_VALIDATION.md).

The research supports implementation work against Intel's specification and
physical-die research. It does not establish byte accuracy or cycle accuracy
for a particular manufactured part.

## Silicon revision: what the evidence supports

Intel's October 1989 datasheet, order 205835-007, identifies 8087, 8087-2 and
8087-1 as 5, 8 and 10 MHz grades and describes HMOS III fabrication. These
names are speed grades in this document; they do not provide a named stepping
or a table of behavioral differences. The document's order-number suffix and
its revision notes must not be promoted to silicon identifiers. See printed
3-90 and the timing tables at 3-102/3-103. [Intel datasheet](https://datasheets.chipdb.org/Intel/x86/808x/datashts/8087/205835-007.pdf#page=1)
([independent archive of the same scan](https://www.ardent-tool.com/CPU/docs/Intel/808x/8087/datasheets/205835-007.pdf#page=1)).

The August 1981 *iAPX 86,88 User's Manual*, order 210201-001, supplies an earlier
8087 supplement. Its publication date identifies a specification edition,
not the revision of an unidentified chip. This search did not establish an
Intel mapping from package/date codes to 8087 silicon steppings, nor a
stepping-specific errata list suitable for selecting a revision. [Intel manual
scan and text](https://archive.org/details/bitsavers_inteldataBrsManual_57011881).

Ken Shirriff's FSCALE report describes opening an actual 8087 and photographing
its die; the report interprets microcode recovered from that physical object.
It does not identify a named silicon stepping or publish a before/after
execution capture for the FSCALE cases discussed. Treat that work as evidence
about the examined die, with an unresolved revision identity. [Shirriff's
FSCALE investigation](https://www.righto.com/2026/09/8087-microcode-reverse-engineering-fscale.html).

Recommendation: use an explicit provisional target description such as
"Intel 8087, stepping unconfirmed, original instruction set, IBM 5150-style
8088 host." Do not invent an A/B/C revision or assume every speed grade has
identical undocumented behavior. Once a physical specimen becomes available,
assign it a specimen identifier and preserve photographs of every package
marking, its board, and the source of the part. Retain an unknown stepping
field until a primary identification source or direct die comparison resolves
it. A date code alone is insufficient evidence of a behavioral revision.

## Initial timing target

The original IBM 5150 Technical Reference describes the 8088 in maximum mode,
a nominal 4.77 MHz processor clock derived from a 14.31818 MHz crystal divided
by three, and an 8-bit external data bus. It gives ordinary bus cycles as four
clocks and I/O cycles as five clocks. These are system starting conditions,
not a claim that every transaction completes in that time. See printed 2-3,
2-10 and 2-113. [IBM Technical Reference, August 1981](https://www.minuszerodegrees.net/manuals/IBM_5150_Technical_Reference_6025005_AUG81.pdf#page=22).

Intel describes instruction-queue monitoring through QS0/QS1, bus ownership
through RQ/GT, READY-based transfer completion, and BUSY connected to TEST for
WAIT synchronization. Its pin and system descriptions are the basis for the
host-interface model. See printed 3-91 through 3-93. [Intel datasheet](https://datasheets.chipdb.org/Intel/x86/808x/datashts/8087/205835-007.pdf#page=2).

The recommended first fixture therefore uses an original Intel 8088, a physical
Intel 8087 rated for the actual clock, the shared board clock, and documented
PC-style bus wiring. Fix and record the motherboard revision before treating
timing results as a baseline. Record refresh/DMA activity, memory location,
READY behavior, interrupt routing, and the exact instruction stream with every
trace. Zero-wait-state isolated timing is a separate experiment, not an assumed
description of a complete PC. Pin propagation delays and clock-edge behavior
also need a stated measurement tolerance; an instruction-cycle count alone
does not validate them.

## Sources inspected for reusable hardware captures

| Source | What was inspected | Qualification and use |
| --- | --- | --- |
| [Opcode Collective / granite 8087 tools](https://github.com/a-mcego/granite/tree/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc) | The directory, its `bin` listing, explanatory notes and decoder source. | Recovered microcode text and interpretation tools. The inspected directory has no state/pin capture corpus. Its notes explicitly retain unknown operations and unresolved entry points. Use to form specific hardware hypotheses; it is not an independently executed reference. |
| [SingleStepTests/8088](https://github.com/SingleStepTests/8088/blob/main/README.md) | Dataset README, register schema and bus-cycle descriptions. | Hardware-generated 8088 CPU tests made with Arduino8088. The documented state covers CPU registers, memory and bus signals, not an 8087 register stack, tags or exception state. Useful for the host CPU model, not an 8087 numerical oracle. |
| [ifilot/8087-asm-math](https://github.com/ifilot/8087-asm-math) | README and recursive source tree. | The author reports testing examples on an IBM 5150. The inspected tree provides assembly examples and helper scripts, with no raw processor-state or logic-analyzer dataset. This is a potential reproducibility/collaboration lead. The claimed run is not a capture available for replay here. |
| [Intel 86/300 Diagnostic Maintenance, May 1983](https://www.hartetechnologies.com/manuals/Intel/144813-004_86-300diag_May83.pdf) | SDT337 description at printed 3-12. | Intel documents a diagnostic for the iSBC 337 Numeric Data Processor and instruction-failure reporting. This is a period test-tool lead. The manual is not a set of measured output states or PC bus traces. |
| [Berkeley Elementary Functions Test Suite](https://citeseerx.ist.psu.edu/document?doi=db9da36a4495562983047e0c857f0327ccebe533&repid=rep1&type=pdf) | Authors' December 30, 1988 report, section 4.2. | Reports running on an IBM PC with an Intel 8087, PC-DOS and Turbo C using extended precision. It tests library functions and does not supply this project's raw per-opcode state or timing oracle. |
| [86Box issue 4518](https://github.com/86Box/86Box/issues/4518) | Diagnostic discussion and linked reproducer description. | First-hand evidence of an emulator diagnostic investigation. A remark mentions a real-hardware failure without enough specimen/input/output provenance to identify an 8087 result. The discussion also reports MCPDIAG using a later instruction. Do not treat its diagnostic pass/fail as a complete original-8087 conformance verdict. |

The [granite explanatory notes](https://github.com/a-mcego/granite/blob/745f0a9c6d4fdfc1b5d6c41b6ae0d2a98a4be03d/tools/8087mc/bin/8087.md)
are especially useful for separating recovered bits from interpretation: they
mark unknown register functions, operation meanings and entry points. A decoder
that prints recovered microcode is not proof that every control signal has
been interpreted correctly.

A follow-up [bus investigation](8088_BUS_TIMING.md) downloaded and inspected
the complete pinned D8 ESC host dataset: 10,000 records from an AMD D8088.
It records the file hash, original provenance, queue-window limits, two-byte
memory reads and prefix reports. These are concrete host bus captures, with
no 8087 registers, BUSY/INT signals or coprocessor arbitration. They do not
change the missing-original-8087 finding above. The external dataset remains
outside the tracked repository.

The search also inspected Shirriff's [2018 die report](https://www.righto.com/2018/08/inside-die-of-intels-8087-coprocessor.html)
and [2026 instruction-decoding report](https://www.righto.com/2026/02/8087-instruction-decoding.html).
These provide first-hand physical analysis. Neither inspected report supplies a
replayable original-hardware execution corpus with a stepping identification.

## Search record and limits

Searches included `8087 stepping`, `8087 revision microcode`, `Intel 8087 errata`,
`8087 real hardware tests results`, `8087 hardware captures`, `8087 traces
FSAVE`, and targeted searches of Intel, Bitsavers, GitHub, Netlib and Berkeley
sources. Relevant repositories and first-hand reports above were followed to
their owners' documentation or source. Search results about USB vendor ID 8087,
SFF-8087 storage cables, ThinkCentre model 8087, modern x87 processors and
software-emulator traces were excluded as target evidence.

A contemporary [BYTE performance article archive](https://manuals.plus/m/3f5e72ba47e77ce7b76899363dd34a8ec42141019a674f5b2340f9ad4d97fd4f)
and collector catalogs surfaced as revision-history leads. They do not replace
an Intel change notice, a specimen's identity or a measured behavioral
comparison. No revision was selected from them. Some archive directory and PDF
requests failed; the Intel datasheet was visually checked using the independent
scan linked above. Search indexing and these access failures limit the negative
finding. Private collections and unindexed attachments remain possible.

## Concrete next actions

1. Keep silicon identification and original-hardware validation open as separate
   requirements. Link every future dataset to a physical specimen and board.
2. Seek an Intel product-change/qualification notice or errata document mapping
   actual 8087 markings to die revisions. Compare the markings of the
   reverse-engineered specimen with any prospective capture specimen before
   combining their evidence.
3. Arrange a run of the existing register-state collector on a documented
   original machine. Preserve the collector, input and output hashes, original
   binaries, case manifest, board/part photographs and the operator's account
   together. No request to another person was sent during this research.
4. Repeat the same input set on independently sourced 8087 specimens. Investigate
   differences as possible specimen, setup, collector or revision effects before
   changing the emulator.
5. Build a separate timing fixture that records the clock, BUSY, queue status,
   READY, address/data bus, bus status and RQ/GT around known instruction
   sequences. Add reset and unmasked-exception/interrupt captures in dedicated
   runs. The present FSAVE collector does not observe those signals.
6. Preserve raw captures and publish reproducible conversion/comparison steps.
   Derived expectations should identify their exact source record, defined-bit
   masks and unresolved fields. Successful model self-comparisons, diagnostic
   runs and modern x87 comparisons cannot be counted as original-8087 evidence.

No files from the external microcode tools, manual scans or third-party test
suites were copied into the implementation as part of this research.
