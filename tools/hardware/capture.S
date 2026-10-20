/* SPDX-License-Identifier: MIT
 * Intel 8086/8088 + 8087, real mode, DOS 3+. GNU as Intel syntax.
 * IMPORTANT: assembled/inspected, NOT executed on original hardware here.
 * Reads VECTORS.BIN, creates (never overwrites) CAPTURE.BIN in current dir.
 * Requires a real, working 8087. Owns and resets FPU state on entry/exit.
 * Captures one documented REGISTER instruction per record. All individual
 * exceptions must be masked, and initial IR/BUSY must be clear: this version
 * is not an unmasked-exception or timing/pin capture program.
 */
.intel_syntax noprefix
.code16
.arch i8086
.section .text
.global _start
_start:
    push cs
    pop ds
    push cs
    pop es
    cld
    mov dx, offset banner
    mov ah, 9
    int 0x21
    .byte 0xdb, 0xe3                  /* FNINIT, no implicit WAIT */
    .byte 0x9b
    mov dx, offset input_name
    mov ax, 0x3d00                  /* open read-only */
    int 0x21
    jnc input_open
    jmp input_error
input_open:
    mov word ptr [input_handle], ax
    mov bx, ax
    mov dx, offset header
    mov cx, 32
    call read_exact
    jnc header_read
    jmp input_error
header_read:
    mov si, offset header
    mov di, offset input_magic
    mov cx, 8
    repe cmpsb
    je magic_ok
    jmp invalid_input
magic_ok:
    cmp word ptr [header+10], 0      /* collector limit: 65,535 records */
    je count_ok
    jmp invalid_input
count_ok:
    cmp word ptr [header+8], 0
    je invalid_near
    cmp word ptr [header+12], 96
    jne invalid_near
    cmp word ptr [header+14], 0
    jne invalid_near
    cmp word ptr [header+18], 15
    ja invalid_near
    mov si, offset header+20
    mov cx, 12
check_reserved:
    cmp byte ptr [si], 0
    jne invalid_near
    inc si
    loop check_reserved
    jmp open_output
invalid_near:
    jmp invalid_input
open_output:
    mov ax, word ptr [header+8]
    mov word ptr [remaining], ax
    mov dx, offset output_name
    xor cx, cx
    mov ax, 0x5b00                  /* DOS 3+: create NEW, fail if it exists */
    int 0x21
    jnc output_open
    jmp output_error
output_open:
    mov word ptr [output_handle], ax
    mov byte ptr [header+3], 'H'     /* I87H0001: collector provenance */
    mov word ptr [header+12], 100
    mov ax, cs
    xor dx, dx
    mov cx, 4
physical_ip:
    shl ax, 1
    rcl dx, 1
    loop physical_ip
    add ax, offset test_slot
    adc dx, 0
    and dx, 15
    mov word ptr [header+16], ax
    mov word ptr [header+18], dx
    mov bx, word ptr [output_handle]
    mov dx, offset header
    mov cx, 32
    call write_exact
    jnc next_record
    jmp output_error
next_record:
    cmp word ptr [remaining], 0
    jne read_record
    jmp finish_input
read_record:
    mov bx, word ptr [input_handle]
    mov dx, offset record
    mov cx, 96
    call read_exact
    jnc record_read
    jmp input_error
record_read:
    /* Mask checks protect the following WAIT from an unhandled exception. */
    mov ax, word ptr [record+2]
    and ax, 0x003f
    cmp ax, 0x003f
    jne bad_record
    mov ax, word ptr [record+4]
    test ax, 0x8080
    jnz bad_record
    call validate_opcode
    jc bad_record
    mov ax, word ptr [record]
    mov word ptr [test_slot], ax
    .byte 0xdd, 0x26               /* FRSTOR [record+2] */
    .word record+2
    .byte 0x9b
    /* Taken jump is essential: flush the 8086/8088 prefetch queue after
       patching the instruction bytes. Do not fall through into stale bytes. */
    jmp test_slot
bad_record:
    jmp invalid_input
test_slot:
    .byte 0xd9, 0xd0               /* patched with ESC and register ModR/M */
    .byte 0x9b
    .byte 0xdd, 0x36               /* FNSAVE [result+6], 94-byte image */
    .word result+6
    .byte 0x9b
    mov word ptr [result], 0       /* completed; no emulator diagnostics */
    mov word ptr [result+2], 0
    mov word ptr [result+4], 0
    mov bx, word ptr [output_handle]
    mov dx, offset result
    mov cx, 100
    call write_exact
    jnc record_written
    jmp output_error
record_written:
    dec word ptr [remaining]
    jmp next_record
finish_input:
    mov bx, word ptr [input_handle]
    mov dx, offset record
    mov cx, 1
    mov ah, 0x3f
    int 0x21
    jc input_error
    or ax, ax
    jnz invalid_input
    mov byte ptr [exit_code], 0
    mov dx, offset success
    jmp cleanup
input_error:
    mov dx, offset input_failure
    jmp cleanup
output_error:
    mov dx, offset output_failure
    jmp cleanup
invalid_input:
    mov dx, offset invalid_failure
cleanup:
    mov ah, 9
    int 0x21
    mov bx, word ptr [input_handle]
    cmp bx, 0xffff
    je close_output
    mov ah, 0x3e
    int 0x21
close_output:
    mov bx, word ptr [output_handle]
    cmp bx, 0xffff
    je exit_program
    mov ah, 0x3e
    int 0x21
    jnc exit_program
    mov byte ptr [exit_code], 1
exit_program:
    .byte 0xdb, 0xe3               /* FNINIT + WAIT: leave no pending IRQ */
    .byte 0x9b
    mov ah, 0x4c
    mov al, byte ptr [exit_code]
    int 0x21
read_exact:
    push cx
    mov ah, 0x3f
    int 0x21
    pop cx
    jc transfer_failed
    cmp ax, cx
    jne transfer_failed
    clc
    ret
write_exact:
    push cx
    mov ah, 0x40
    int 0x21
    pop cx
    jc transfer_failed
    cmp ax, cx
    jne transfer_failed
    clc
    ret
transfer_failed:
    stc
    ret
/* Whitelist matches tools/trace_format.h; reject later/undocumented opcodes. */
validate_opcode:
    mov al, byte ptr [record]
    mov dl, byte ptr [record+1]
    cmp dl, 0xc0
    jb opcode_bad
    cmp al, 0xd8
    je opcode_good
    cmp al, 0xd9
    je opcode_d9
    cmp al, 0xdb
    je opcode_db
    cmp al, 0xdc
    je opcode_dc
    cmp al, 0xdd
    je opcode_dd
    cmp al, 0xde
    je opcode_de
opcode_bad:
    stc
    ret
opcode_good:
    clc
    ret
opcode_db:
    cmp dl, 0xe0
    jb opcode_bad
    cmp dl, 0xe3
    ja opcode_bad
    jmp opcode_good
opcode_dc:
    mov al, dl
    and al, 0x38
    cmp al, 0x10
    je opcode_bad
    cmp al, 0x18
    je opcode_bad
    jmp opcode_good
opcode_dd:
    mov al, dl
    and al, 0x38
    cmp al, 0
    je opcode_good
    cmp al, 0x10
    je opcode_good
    cmp al, 0x18
    je opcode_good
    jmp opcode_bad
opcode_de:
    cmp dl, 0xd9
    je opcode_good
    jmp opcode_dc
opcode_d9:
    cmp dl, 0xd0
    jbe opcode_good
    cmp dl, 0xe0
    je opcode_good
    cmp dl, 0xe1
    je opcode_good
    cmp dl, 0xe4
    je opcode_good
    cmp dl, 0xe5
    je opcode_good
    cmp dl, 0xe8
    jb opcode_bad
    cmp dl, 0xee
    jbe opcode_good
    cmp dl, 0xf0
    jb opcode_bad
    cmp dl, 0xf4
    jbe opcode_good
    cmp dl, 0xf6
    jb opcode_bad
    cmp dl, 0xfa
    jbe opcode_good
    cmp dl, 0xfc
    je opcode_good
    cmp dl, 0xfd
    je opcode_good
    jmp opcode_bad

input_name: .asciz "VECTORS.BIN"
output_name: .asciz "CAPTURE.BIN"
input_magic: .ascii "I87V0001"
banner: .ascii "8087 register-state collector (requires physical 8087).\r\n$"
success: .ascii "Captured CAPTURE.BIN. Record chip/board provenance separately.\r\n$"
input_failure: .ascii "Input read/open failure; partial output must not be trusted.\r\n$"
output_failure: .ascii "Output exists or create/write failed. No existing file overwritten.\r\n$"
invalid_failure: .ascii "Invalid/unsafe input: require masked, nonpending, documented register tests.\r\n$"
input_handle: .word 0xffff
output_handle: .word 0xffff
remaining: .word 0
exit_code: .byte 1
header: .space 32, 0
record: .space 96, 0
result: .space 100, 0
