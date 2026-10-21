# Accuracy and remaining gaps

## Claim boundary

This is an **experimental instruction-level implementation**, not a completed
bit-exact or cycle-exact Intel 8087. No original 8087 was available for execution,
no physical-hardware trace corpus was used, and no target silicon revision has
been selected. The supplied capture probe has not been run on physical hardware.

Physical hardware access is outside the required scope. Completion is assessed
from original documentation, first-hand online research and comprehensive
independent software tests. Published captures are optional corroboration.
The absence of private hardware alone is not a blocker; unresolved behavior,
missing implementation and unsupported accuracy claims still matter.

The tests establish agreement with sampled exact mathematical calculations,
document-derived instruction expectations and API invariants. They do not
establish agreement with all 8087 datapath, microcode, reserved-bit, asynchronous
exception or electrical behavior. Passing counts must not be presented as an
8087 compatibility percentage.

## Coverage matrix

| Area | Implemented and exercised | Remaining qualification |
| --- | --- | --- |
| State | Eight raw 80-bit physical registers, TOP, tags, control/status and pointers | Public struct is not packed; arbitrary inconsistent tags/values are not certified |
| Transfers | Register/tag-preserving transfers; 32/64/80-bit real, signed integer, valid packed BCD formats | Noncanonical/invalid BCD cases and exact NaN payload rules need further source research and reference tests |
| Finite arithmetic | Integer-software arithmetic; separate original add/subtract datapath for stored alignment, significance gates, fraction/GRS arithmetic and PC24/53/64 rounding | Nonzero alignment shifts of at least 64, cancellation C1, pseudozero inputs and unnormal range completion remain guarded; multiply/divide still use canonical fallbacks for unnormals |
| FXTRACT | Raw-field decomposition, signed-zero pair, finite unnormal/pseudozero handling | Infinity/NaN result pairs remain guarded; finite results are document-derived, not silicon-certified |
| FPREM | Complete finite remainder and modeled quotient history; precision-control independent; stored-exponent long reductions remain guarded even when canonical normalization would finish | Partial divider/count controls, raw unnormal quotient history and tiny-result range processing remain unresolved |
| FSCALE | Zero-tag short circuits, in-range exponent-only scaling, original finite scale domain | Several exceptional/range inputs remain guarded or unverified |
| FPTAN | Zero and positive normal operands with stored exponent <= 0x3fbf bypass the fallback | Full CORDIC/rational datapath, rounding, two raw outputs and flags remain approximate |
| Other transcendentals | F2XM1 canonical signed-zero early return; explicit opt-in fallbacks for other inputs | General F2XM1/FYL2X/FYL2XP1/FPATAN algorithms are not original microcode; no original-result bit-equivalence claim |
| Constants | FLD1 and FLDZ exact; other constant encodings have table entries | Other constants' original ROM bits and loading/rounding behavior remain guarded |
| Exceptions | Separate sticky flags and event latch; before/after distinction; unmasked PE commits result/store/pop | Unmasked invalid comparison continuation and some special-result paths remain guarded; no asynchronous microinstruction model |
| Range | Canonical unmasked register exponent wrapping; original exact-tiny UE; suppressed unmasked real stores | Reduced-PC masked underflow, CHOP-overflow disagreement, and malformed/unnormal range paths still need investigation |
| Environment | Fourteen-byte environment and 94-byte save/restore; original 20-bit pointer layout | Reserved-bit readback and malformed restore images not hardware-certified |
| Instruction decoding | Documented ESC forms, including the nine alternate groups in Intel Table A-2; rejection of later extensions | Reserved decoder behavior remains unresolved; full decoder smoke test is not an ISA oracle |
| Timing/electrical | No implementation | No cycle count, prefetch snooping, queue, dummy read, DMA/arbitration, pin transition or execution-overlap model |

## Known-gap policy

`I8087_STOP_ON_UNVERIFIED` is the default. A guarded path returns
`I8087_UNVERIFIED` and updates `last_gap` but does not commit register, control,
status, pointer, stack or memory-write changes. An operand read may have occurred.
This is an emulator diagnostic, NOT an exception a real 8087 would generate.

`I8087_ALLOW_APPROXIMATION` enables supported fallbacks. `last_gap` describes the
current instruction; `used_approximations` accumulates committed categories.
Some undefined operands still stop even in this policy. The absence of a gap
flag means only that no implemented guard fired, not that the result has been
validated on original hardware.

| Flag | Current guard/fallback |
| --- | --- |
| `I8087_GAP_TRANSCENDENTAL` | General software transcendental algorithms in place of original microcode; excludes canonical signed-zero F2XM1 and the tiny/zero FPTAN bypass |
| `I8087_GAP_UNNORMAL` | Remaining significance, large-alignment, cancellation and tiny-conversion paths; excludes the supported original finite-add datapath and bounded unnormal-plus-zero arithmetic |
| `I8087_GAP_RANGE_EXCEPTION` | Remaining range paths, including reduced-PC masked underflow, masked CHOP overflow, and unsupported remainder/range combinations |
| `I8087_GAP_EXCEPTION_RESTART` | Unmasked invalid comparison continuation and register range cases without a supported wrapped result |
| `I8087_GAP_PARTIAL_REMAINDER` | Bounded software partial reduction instead of original microcode sequence |
| `I8087_GAP_CONSTANT_ROM` | Fixed nearest binary80 table entry rather than validated original load behavior |
| `I8087_GAP_RESERVED_PRECISION` | PC=01 falls back to 64 bits when precision selection is relevant |
| `I8087_GAP_UNDEFINED_OPERAND` | Unsupported/undefined original domains; many such cases stop unconditionally |
| `I8087_GAP_STACK_FAULT` | Masked indefinite-result fallback for stack overflow/underflow |

The guard is not exhaustive. In particular, arbitrary restored tags, pseudo-
infinities, malformed NaNs, exact NaN tie selection, range-edge encodings,
reserved/undefined condition bits and reserved-word readback are not all detected.
Numeric underflow or overflow agreement is not proof of raw-state agreement in
all boundary cases. Default strict mode must not be called a 1:1 mode.

## Original-processor distinctions retained

- Control bit 7 is the global interrupt mask and bit 12 selects projective/affine
  infinity. Status bit 6 is reserved, not the later x87 stack-fault flag.
- Cold API initialization clears storage. FNINIT and FSAVE preserve existing
  data-register bytes and pointers while resetting the modeled control/tags.
- Initial control is `03bf`, with reserved bit 6 zero. This is a deterministic
  choice, not a claim about original-silicon reserved-bit readback. FLDCW and
  FLDENV similarly clear reserved bits.
- FSTENV saves the old environment, then masks individual exceptions without
  changing the global interrupt mask.
- Short/long-real denormals load as original-style unnormal temporary values.
  Their numeric interpretation is not the complete lost-significance model.
- Completed FPREM can shift previous C3/C1 into its quotient result for small
  quotients. Partial reduction is still explicitly unverified.
- The general FPTAN fallback returns approximate sine/cosine-derived values;
  those two raw register values are not the original partial-tangent outputs.
- Interrupt/WAIT behavior is instruction-atomic and functional. It does not
  reproduce internal microinstruction interrupt timing or pin waveforms.
- Masked directed overflow follows Intel S-75: raise PE without newly setting
  OE, preserving an old sticky OE. A normal finite register saturation bound
  has all 64 significand bits set after PC rounding. Memory bounds retain their
  format widths. These are source-derived policies awaiting original captures;
  [the investigation](research/DIRECTED_OVERFLOW.md) records the printed
  discrepancy and decoded-path evidence.

## Evidence required for a 1:1 claim

Select a target original part/revision. Validate documented instructions and
relevant aliases against preserved raw-state captures across register classes,
all control modes, exception priorities, stack faults, memory formats, constant
loads and transcendental boundaries. Replace or validate guarded fallback paths.
Where documentation and die interpretation disagree, preserve the disagreement
until a targeted capture or circuit-level derivation resolves it.

The collector in this release is only a starting point: masked, register-form,
single-instruction captures. It does not cover unmasked exception handlers,
memory transfers, instruction sequences or timing. Those require additional
capture fixtures and tests. Cycle- or pin-exact claims also require a separate
validated execution/bus model. See HARDWARE_VALIDATION.md.
