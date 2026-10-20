# Integrating the core

Include `i8087.h` and compile/link all three implementation files. Each `i8087`
object owns its state. Independent instances do not share rounding modes or
mutable numerical globals. Access to the same instance must be serialized.

## Initialization

```c
#include "i8087.h"

i8087 fpu;
i8087_init(&fpu); /* Cold start, default STOP_ON_UNVERIFIED policy. */
```

An API cold start clears storage. Executing DB E3 (FNINIT) is different: it
performs the modeled architectural initialization without clearing the existing
data-register bytes and instruction/data pointers.

`registers[0..7]` are physical registers. ST(i) addresses
`(i8087_top(&fpu) + i) & 7`. Use `i8087_get_st()` and `i8087_tag()` to inspect
logical registers. `i8087_get_st()` returns false for an empty register, but
empty physical register bytes remain available for save/restore.

The `i8087_f80` struct may have C padding. Never `memcpy` it into an emulated
10-byte operand. Use `i8087_encode_f80()` and `i8087_decode_f80()`, which explicitly
encode/decode the little-endian significand followed by sign/exponent.

## Memory callbacks and addresses

```c
i8087_bus bus = {
    .context = machine,
    .read = read_operand,
    .write = write_operand
};
```

The callback signatures are declared in the header. Each call transfers an
entire operand, up to 94 bytes. The passed start address is a 20-bit physical
address. Implement physical address incrementing and 1 MiB wrap in the callback
when modeling an 8086/8088 system. Do **not** wrap operand transfers at a 64 KiB
segment boundary. The small demo uses deliberately bounded flat memory instead.

A false write callback must leave memory unchanged. The core stages a write
until its instruction checks succeed and does not commit CPU-visible state if
the callback fails. It cannot undo partially performed writes in a faulty
callback. Reads may happen before a known-gap rejection, so side-effect-free
reads are required for safe retries.

Callbacks must not recursively execute the same FPU object. Different instances
can be used by different threads when their callbacks and memories are also
properly isolated or synchronized.

## Executing an instruction

```c
i8087_result result = i8087_execute(
    &fpu, &bus,
    0xd8, 0xc1,     /* FADD ST(0), ST(1) */
    instruction_physical_address,
    0              /* Ignored for this register instruction. */
);
```

The host CPU decodes prefixes, the ModR/M addressing mode and displacement. It
passes an ESC opcode in D8..DF, the ModR/M byte, and the resolved addresses. The
FPU core does not fetch CPU instruction bytes or advance a host instruction
pointer. The caller advances execution only after processing the result.

For a preceding 9B WAIT/FWAIT, call `i8087_wait()` first. If it reports a pending
interrupt, the host must handle that condition rather than blindly dispatching
the following opcode. There is no asynchronous execution clock: an accepted
instruction completes within the call, apart from the modeled pending exception.

## Return values

| Result | Contract |
| --- | --- |
| `I8087_OK` | Instruction state and any staged operand write were committed |
| `I8087_PENDING_INTERRUPT` | An unserviced WAIT blocked dispatch, WAIT found a pending event, or execution raised an unmasked exception |
| `I8087_UNVERIFIED` | Known-gap rejection; only the diagnostic `last_gap` is updated, not architectural instruction state |
| `I8087_BAD_OPCODE` | Encoding is not in the documented 8087 instruction set implemented here |
| `I8087_MEMORY_FAULT` | Callback missing or operand transfer failed; no FPU state commit |
| `I8087_BAD_ARGUMENT` | Null FPU or ESC opcode outside D8..DF |

`last_gap` is set on a successful instruction or an unverified rejection. An
invalid encoding, failed transfer or pre-existing pending interrupt does not
necessarily reset it. Inspect it in conjunction with the current result.

A pending interrupt is not a generic "retry the same instruction" result.
When an unserviced WAIT blocks dispatch, nothing executes. Otherwise an
instruction raising an unmasked exception commits the modeled exception state:

| Exception stage | Result/store/stack action |
| --- | --- |
| Before-operation IE/DE/ZE | Preserve input registers, tags and TOP; suppress store |
| After-operation PE | Keep the rounded result, completed store and pop |
| Supported unmasked register OE/UE | Keep the rounded significand with exponent wrapped by -/+24,576; retain the instruction's pop |
| Unmasked real-store OE/UE | No memory write or pop; no subsequent precision stage |

Some unmasked invalid comparisons and unsupported register range cases still
require approximate mode; see ACCURACY.md. Existing sticky flags do not become
new events just because FLDCW changes masks. Masking a pending event does not
clear the latched IR. FCLEX/FINIT are explicit clearing operations.

The host should synchronize at WAIT before dispatching the following CPU
instruction. It must distinguish that blocked synchronization point from an
already-dispatched ESC that reported a new exception; blindly retrying an
unmasked PE instruction could repeat a committed store or pop. No-WAIT ESCs can
execute while an event is latched when the CPU is not held at WAIT. These are
functional hooks, not an asynchronous bus/queue model.

`i8087_interrupt_asserted()` exposes the modeled interrupt state, including the
WAIT/mask interaction. It does not report a cycle-timed electrical signal.

## Explicitly allowing approximations

```c
i8087_set_policy(&fpu, I8087_ALLOW_APPROXIMATION);
/* Execute, then inspect fpu.last_gap and fpu.used_approximations. */
```

Use this only with the accuracy matrix understood. Out-of-domain transcendental
arguments and some other undefined operands still return `I8087_UNVERIFIED`.
The default policy is preferable when silent approximations would undermine a
compatibility test.

## Serialization

Use FSTENV/FLDENV for the 14-byte architectural environment and FSAVE/FRSTOR for
the 94-byte architectural save image. These do not serialize host policy,
accumulated diagnostics, callback state, or waiting state as a portable host
snapshot. In-process copies of `i8087` are possible, but raw struct bytes are
not a portable on-disk format.
