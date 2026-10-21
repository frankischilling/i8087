# Original 8088/8087 bus timing

Research date: 2026-10-20. No physical 8087 bus trace was obtained.

## What the evidence supports

A passive bus and instruction-queue observer is a useful first subsystem for
the requested PC-style target. It can reconstruct fetched bytes, queue removals,
ESC encodings and CPU operand reads from supplied clocked inputs. This does not
establish instruction execution latency, the 8087's internal queue pipeline,
BUSY/INT timing or a complete cycle-accurate coprocessor.

## Original pin timing

Intel's passive-mode waveform specifies falling-CLK setup/hold for QS
(`TQVCL`/`TCLQX`, 30/10 ns on the plain 8087) and addresses/data
(`TDVCL`/`TCLDX`). It places address capture at T1's end and data capture at the
final T3/Tw's end before T4. Latch the bus-cycle kind: S2:S0 can already be
passive when data arrives.
[Intel 8087 datasheet, printed pages 3-102 through 3-106](https://kib.kiev.ua/x86docs/Intel/486/205835-007.pdf#page=13).

READY is sampled near T2, T3 and Tw's ends. Wait states must not duplicate a
completed fetch or suspend QS sampling. RESET needs at least four high clocks;
the waveform shows at least eight clocks before tracking and twenty before
execution readiness. These are minima, not a fixed reset countdown.
[Intel 8087 datasheet, master and reset waveforms](https://kib.kiev.ua/x86docs/Intel/486/205835-007.pdf#page=15).

Inference for simulator scheduling: sample stable pre-edge pins at falling CLK,
then propagate new CPU outputs. Taking freshly changed QS on the same edge
would bypass setup/hold. The waveform specifies input capture, not the later
internal queue-write edge.

## Queue and operand semantics

QS1:QS0 reports `00` no removal, `01` first opcode byte, `10` flush and `11`
subsequent byte. Reports describe the preceding CPU clock and are independent
of bus activity. Intel AP-67 says the CPU changes QS after falling CLK and
external logic may sample it at rising CLK. That generic suggestion does not
replace the 8087's falling-edge input specification above. Simultaneous fetch
and removal are possible.
[Intel AP-67, printed A-13 through A-14 and A-39](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=316).

After RESET, identify the CPU from pin 34 during its first instruction fetch:
high selects the 8088; low selects the 8086. This is not an arbitrary sample
while RESET is high. FINIT and FSAVE do not restart CPU tracking. A memory ESC
causes the CPU to read a word. The 8087 retains the first physical address;
loads capture that word and stores ignore its data. On the 8088 this takes two
byte cycles. Longer loads continue through coprocessor bus ownership.
[Intel numerics supplement, S-22 and S-26](https://archive.org/download/bitsavers_inteldataBrsManual_57011881/1981_iAPX_86_88_Users_Manual.pdf#page=705).

## Patent and die cross-checks

Intel's original coprocessing patent separates a replicated queue, instruction
address, temporary instruction address and operand address. Figure 9 derives
read-queue, first-byte, subsequent-byte and flush controls from QS. Figure 10
decodes code fetch separately from memory read. Its operand-address load is
gated by internal T2/phase timing, and a latch disables further memory-address
loads after the first T3 until cleared. This supports retaining the first read
address rather than repeatedly overwriting it with each byte transfer. The
patent describes an embodiment; its internal phase names do not by themselves
identify packaged 8087 clock edges or an 8088-specific implementation.
[Intel patent US4270167A, figures 8 through 10 and accompanying description](https://patents.google.com/patent/US4270167A/en).

First-hand die analysis finds separate BIU opcode registers and a replicated
queue. It reports six physical byte registers with two tied together, leaving
five usable entries on the examined die. The proposed explanation involving
separate active-instruction storage is explicitly a hypothesis. Representing
the host's four-byte FIFO externally is useful for reconstruction; it is not
evidence that the original 8087 physically implements a four-entry ring buffer.
[Ken Shirriff, instruction decoding, notes 2 and 3](https://www.righto.com/2026/02/8087-instruction-decoding.html).

## Available captures and their limits

The [SingleStepTests 8088 corpus](https://github.com/SingleStepTests/8088) includes
downloadable D8 through DF ESC datasets. At commit
`aea84484abc79d09639d855b7b0ab32bc9e4dbeb`, each contains 10,000 records. The author
identifies an AMD D8088 marked `8441DMA`, with emulated bus-controller outputs,
no wait states and no interrupt/trap coverage. These are host CPU captures,
not measurements of an Intel 8088 and physical 8087 pair. WAIT is omitted.
[Corpus provenance and format](https://github.com/SingleStepTests/8088/blob/aea84484abc79d09639d855b7b0ab32bc9e4dbeb/README.md).

An inspected [D8 capture file](https://github.com/SingleStepTests/8088/blob/aea84484abc79d09639d855b7b0ab32bc9e4dbeb/v2/D8.json.gz)
has SHA-256 `87b8a64fb24ed616f00e272d6caac6f67a8ca9251c1b783b7f68465e9a9bde2c`.
Record index 0, hash `5b970c9bc1c97047379c2b5dae1487ac1be7de43`, starts with a
prefilled queue containing `D8 2D 90 90`. It reports two operand reads at
decimal addresses 212570 and 212571, returning `07` and `3F`. Unprefilled
records can begin after the opcode fetch. A replay adapter must establish the
trace-window state explicitly. The format's BHE field is a placeholder on the
8088, not a physical pin-34 host-identification measurement.

A full-file inspection found 7,477 memory forms and 2,523 register forms, with
5,000 prefilled and 5,000 empty initial queues. Of the 10,000 rows, 4,972 have
a segment prefix. Latching the cycle kind and physical address at T1 and
reading data at T3 produces two MEMR bytes for each memory form and none for
each register form. Every observed second address is the first plus one
modulo the physical address space, and every read byte matches the initial
RAM image. No inspected read crosses a physical 64 KiB boundary; this file
therefore does not settle segment-offset or address-wrap edge behavior.

The inspection skips the optional segment prefix before selecting ModR/M.
Treating byte 1 as ModR/M unconditionally misclassifies prefixed instructions.
Record 10 contains `26 D8 AD D0 21`; its removal reports are `F:26`, `F:D8`,
`S:AD`, `S:D0`, `S:21`. Thus the host trace reports first-byte status for both
the prefix and the ESC opcode. An observer must handle this sequence without
discarding the instruction or treating the prefix as an ESC operand byte.
These checks establish properties of the pinned AMD capture file, not Intel
8087 pin timing or execution.

Andrew Jenner's [ISA bus sniffer report](https://www.reenigne.org/blog/isa-bus-sniffer-update/)
includes an attributable XT bus-trace excerpt and links to the capture harness
and decoder. The probe occupies the 8087 socket to observe CPU pins. That
socket location does not make the example an 8087 execution capture. The
bounded search found no raw trace of an identified physical 8087 performing
bus arbitration, operand transfers and BUSY/INT transitions.

## Concrete implementation boundary

Implement a separate, passive observer before connecting it to execution:

1. Accept explicit clock edges, pin values and ownership. Keep tracking state
   separate from numeric initialization and saved floating-point state.
2. Record successful CPU code fetches with address and byte. Preserve fetched
   bytes through later memory writes; do not reread code memory at decode time.
3. Consume only reported QS removals. On a flush, discard pending prefetched
   bytes and incomplete instruction decoding. Permit fetch and removal together.
4. Decode ESC from consumed bytes. Observe ModR/M and displacement removals;
   obtain the effective address from the CPU's subsequent read, not from a
   second implementation of CPU registers or address arithmetic.
5. Report both dummy-read byte transfers while retaining the instruction's
   original operand address. Do not execute a store on either CPU read.

Test these contracts with explicit synthetic waveforms, then replay attributable
host traces with their capture-window assumptions recorded. Cover queue wrap,
flush with a fetch in flight, stale prefetched code, idle clocks, wait states,
prefixes, back-to-back ESC instructions and the two operand byte cycles. Flag
missing samples and impossible queue operations rather than inventing data.

Before claiming exact internal timing, resolve queue-write versus removal
ordering at coincident edges, flush versus an outstanding fetch, opcode-latch
pipeline delays, RESET synchronization and instruction buffering while the NEU
is busy. Active bus ownership additionally needs request/grant timing and
READY-dependent transfer sequencing. Original 8087 traces or a complete circuit
derivation remain necessary for those claims. Numeric latency and execution
overlap remain separate unfinished work.
