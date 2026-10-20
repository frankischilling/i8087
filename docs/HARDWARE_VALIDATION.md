# Original-hardware capture and model replay

## What this workflow does

The package includes an **unverified-on-hardware** DOS probe for an 8086/8088
machine with a working, physical Intel 8087. It loads a raw 94-byte environment,
executes one two-byte register-form instruction, waits, and saves the resulting
94-byte state. A separate C tool replays the same inputs in this emulator, and
Python compares the two outputs.

The probe was assembled successfully with GNU binutils 2.44 and its instruction
stream was inspected. It has NOT been run under DOS or on a real 8087 in this
work. No CAPTURE.BIN or other file claimed as hardware output is supplied.

This is not a hardware detector. Running it without a working 8087 can hang at
WAIT. Use a dedicated test session: it owns the FPU and resets its state on entry
and exit; it does not preserve another application's FPU context. It creates
files only in the current directory and refuses to overwrite CAPTURE.BIN.

## Prepare inputs and collector

From the source root on a development system, use a fresh output directory
for each capture campaign. The generator writes its three output files, so do
not rerun it over a directory containing preserved inputs/provenance:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 tools/make_vectors.py captures/smoke --suite smoke
sh tools/hardware/build.sh
```

The generator writes three files:

| File | Meaning |
| --- | --- |
| `VECTORS.BIN` | Raw input states and register instruction bytes; NOT expected results |
| `cases.json` | Case names, indices and SHA-256 of the input file |
| `capture-metadata.template.json` | Unfilled provenance form, explicitly not a capture |

The smoke suite has 388 input cases; the research suite has 48. `--suite research` selects deliberately
unverified areas, and `--suite all` combines the suites. Input generation alone
establishes nothing about the chip or the emulator's accuracy.

`tools/hardware/CAPTURE.COM` is supplied together with its assembly source. The
build script can reproduce it using GNU `as` and `ld` with 32-bit ELF support;
`.code16` and `.arch i8086` select the real-mode instruction encoding. No DOS C
runtime, host floating point or external library is used by the probe.

## Collect on the physical target

Copy CAPTURE.COM and the selected VECTORS.BIN into an otherwise disposable
working directory on the original machine. DOS 3 or later is required for the
create-new-file operation. Run:

```text
CAPTURE.COM
```

The expected successful artifact is CAPTURE.BIN. A nonzero exit code, error
message, interrupted run or missing record invalidates the run. The readers
reject a file whose size does not match its declared count. Preserve the exact
input file and case manifest alongside the capture.

Fill a copy of the provenance template with the chip's package marking,
identified revision if known, motherboard, 8086/8088 part, clock, DOS version,
capture date and any modifications. Record SHA-256 hashes of the collector,
input and capture on the development system. A photograph of the part and board
can support identification, but does not itself validate execution.

A modern x87, an 80287, DOSBox or any other emulator is NOT a physical 8087
reference. Such runs may test tooling, but must be labeled model/system tests.
The file's marker records the producing tool, not cryptographic proof of origin.

## Replay and compare

Copy CAPTURE.BIN back next to its original VECTORS.BIN and run:

```sh
./build/i8087_replay captures/smoke/VECTORS.BIN captures/smoke/MODEL.BIN \
  --reference captures/smoke/CAPTURE.BIN
python3 tools/compare_traces.py captures/smoke/VECTORS.BIN \
  captures/smoke/CAPTURE.BIN captures/smoke/MODEL.BIN \
  --json captures/smoke/report.json
```

The collector records the physical address of its patched test instruction.
`--reference` supplies that same address to the emulator so pointer fields can
be compared instead of reporting a false address mismatch. Reference/header
count agreement does not prove the inputs were identical: preserve and check
the input hash in your provenance record.

The comparator reports matched, blocked, approximated and mismatched cases.
A blocked case is not a pass. A committed approximation is counted separately
even if its bytes happen to match. A mismatch includes the field name, byte
offset, actual/reference byte and mask. A dataset comparison is not a universal
1:1 certificate, even if every record matches.

Exit codes: 0 means every record matched with no blocked/approximate records;
1 means a mismatch, blocked case or approximation; 2 means a malformed input,
provenance mismatch or usage error. The approximate count may overlap the
matched/mismatched count; it is a qualification, not an additional partition.

Default comparison masks out reserved control/environment bits, status bit 6
and undefined condition bits for the selected instruction. It checks all raw
register bytes, including empty-register storage. Add `--all-bits` to compare
all 94 bytes, including reserved/unspecified values. Both views matter: the
masked comparison can focus documented differences, but cannot establish exact
reserved-bit behavior.

To inspect fallback output, add `--approximate` to i8087_replay. Gap flags remain
present, and the comparison still does not count those rows as proven passes.

## Model-only tooling self-test

This exercises the file protocol without inventing hardware evidence:

```sh
./build/i8087_replay captures/smoke/VECTORS.BIN captures/smoke/MODEL.BIN
python3 tools/compare_traces.py captures/smoke/VECTORS.BIN \
  captures/smoke/MODEL.BIN captures/smoke/MODEL.BIN --self-test --all-bits
```

Without the explicit `--self-test` switch the comparator rejects a MODEL file
as the reference. The Python writer cannot emit the HARDWARE marker. A self-test
only proves tooling consistency, not agreement with an original processor.

## Binary format, version 1

All integers are little-endian. Header size is 32 bytes:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `I87V0001` inputs, `I87M0001` model output, `I87H0001` collector output |
| 8 | 4 | Number of records |
| 12 | 2 | Record size: 96 input or 100 output |
| 14 | 2 | Flags, currently zero |
| 16 | 4 | Physical instruction address, at most 0xfffff |
| 20 | 12 | Reserved, all zero |

Input record: ESC byte, register ModR/M byte, then 94-byte original FSAVE image.
Output record: 16-bit `i8087_result`, 32-bit gap mask, then the 94-byte image.
The collector writes result/gaps zero only after the instruction and save have
completed their WAIT synchronization. Model output retains rejection/pending
results and gap flags. Register bytes in FSAVE order are logical ST(0)..ST(7);
the saved tag word is physical and must be interpreted using TOP.

Host tools permit 1 through 1,000,000 records; the DOS probe permits 1 through
65,535. Empty datasets are rejected rather than treated as vacuous passes.
Trailing data, truncated records, nonzero reserved header fields and unsupported
opcodes are rejected. A failed run may leave an incomplete output file; do not
reuse it as evidence.

## Limitations and next fixtures

The collector accepts only documented register-form ESC instructions, with all
six individual exceptions masked and initial IR/BUSY clear. It deliberately
rejects memory instructions, undocumented/later aliases and unmasked tests.
The test slot is rewritten and followed by a taken jump to flush the 8086/8088
prefetch queue before execution.

This release does not collect unmasked exception-handler entry/continuation,
external memory transfers, multi-instruction sequence histories, clock counts,
pins, queues or bus arbitration. Those need separate fixtures and validation.
It also does not automatically bind a capture to a chip photograph or authenticate
its provenance. Keep the source inputs, program hash, raw capture and machine
metadata together instead of promoting unlabeled numbers to hardware truth.
