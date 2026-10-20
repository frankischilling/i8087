#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare inputs for a PHYSICAL 8087 collector. Generates no expected outputs."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from trace_format import INPUT_MAGIC, vector, write_trace

J = 1 << 63

def integer(n: int) -> tuple[int, int]:
    if not n:
        return 0, 0
    sign, mag = n < 0, abs(n)
    e = mag.bit_length() - 1
    return (int(sign) << 15) | (16383 + e), mag << (63 - e)

def make_cases(suite: str) -> tuple[list[bytes], list[dict]]:
    records, names = [], []
    def add(name, esc, modrm, a, b=None, cw=0x3BF, sw=0, **kwargs):
        records.append(vector(esc, modrm, a, b, cw, sw, **kwargs))
        names.append({"index": len(records) - 1, "name": name, "esc": esc, "modrm": modrm})
    if suite in ("smoke", "all"):
        for pc in (0, 2, 3):
            for rc in range(4):
                cw = 0xBF | pc << 8 | rc << 10
                for op, modrm in (("FADD", 0xC1), ("FSUB", 0xE1), ("FMUL", 0xC9), ("FDIV", 0xF1)):
                    for a in (-9, -1, 1, 9):
                        add(f"{op} {a}/3 pc={pc} rc={rc}", 0xD8, modrm, integer(a), integer(3), cw)
                for sign in range(2):
                    add(f"F2XM1 signed zero sign={sign} pc={pc} rc={rc}",
                        0xD9, 0xF0, (sign << 15, 0), cw=cw)
        for a in range(18):
            for old in range(8):
                cc = (0x100 if old & 4 else 0) | (0x4000 if old & 2 else 0) | (0x200 if old & 1 else 0)
                add(f"FPREM {a}%3 old-quotient={old}", 0xD9, 0xF8, integer(a), integer(3), sw=cc | 0x400)
        for sign in (0, 0x8000):
            for exp in (0, 1, 0x3FFE, 0x3FFF, 0x4000):
                for sig in (0, 1, J, J + 1):
                    add(f"FXTRACT {sign|exp:04x}:{sig:016x}", 0xD9, 0xF4, (sign | exp, sig))
        for delta in (-10, -1, 0, 1, 10):
            add(f"FSCALE raw unnormal by {delta}", 0xD9, 0xFD, (0x3FFF, J >> 4), integer(delta))
        add("FSCALE zero/empty short circuit", 0xD9, 0xFD, (0, 0))
        for exp in (1, 0x3F00, 0x3FBF):
            add(f"FPTAN tiny {exp:04x}", 0xD9, 0xF2, (exp, J + 17))
        add("FPTAN +zero", 0xD9, 0xF2, (0, 0))
        add("FPTAN -zero", 0xD9, 0xF2, (0x8000, 0))
        add("pseudozero FXAM", 0xD9, 0xE5, (0x4000, 0))
    if suite in ("research", "all"):
        for rc in range(4):
            for opcode, name in ((0xE9, "FLDL2T"), (0xEA, "FLDL2E"), (0xEB, "FLDPI"),
                                 (0xEC, "FLDLG2"), (0xED, "FLDLN2")):
                add(f"{name} rc={rc}", 0xD9, opcode, (0, 0), cw=0x3BF | rc << 10, tags=0xFFFF)
        for exp, sig in ((0x3FC0, J + 1), (0x3FD7, J + 123), (0x3FEE, J), (0x3FEF, J),
                         (0x3FFD, J), (0x3FFE, J), (0x3FFE, J + (J >> 1))):
            add(f"FPTAN CORDIC/rational {exp:04x}:{sig:016x}", 0xD9, 0xF2, (exp, sig))
        for x in ((0, 0), (0x3FFD, J), (0x3FFE, J)):
            add(f"F2XM1 {x}", 0xD9, 0xF0, x)
        for x in (integer(1), integer(2), (0x3FFE, J)):
            add(f"FYL2X {x}", 0xD9, 0xF1, x, integer(3))
        add("FYL2XP1 +1/8", 0xD9, 0xF9, (0x3FFC, J), integer(3))
        add("FYL2XP1 -1/8", 0xD9, 0xF9, (0xBFFC, J), integer(3))
        add("FPATAN 1/4", 0xD9, 0xF3, integer(1), (0x3FFD, J))
        for exp in (0x403F, 0x4040, 0x4100, 0x7FFE):
            add(f"FPREM partial reduction exponent {exp:04x}", 0xD9, 0xF8, (exp, J + 123), integer(3))
        for modrm in (0xC1, 0xC9, 0xF1):
            add(f"unnormal arithmetic {modrm:02x}", 0xD8, modrm, (0x4001, J >> 5), integer(3))
        add("masked CHOP overflow", 0xD8, 0xC9, (0x7FFE, J), integer(2), cw=0xFBF)
        add("masked reduced-PC underflow", 0xD8, 0xC9, (1, J + 1), (0x3FFE, J), cw=0x2BF)
        add("FXTRACT infinity pair", 0xD9, 0xF4, (0x7FFF, J))
        add("masked stack read underflow", 0xD8, 0xC1, (0, 0), integer(1), tags=0xFFFF)
        add("masked stack push overflow", 0xD9, 0xE8, integer(1), integer(1),
            other=(integer(1),) * 6, tags=0)
    if suite in ("frndint", "all"):
        operands = ((0x403E, 1), (0x4040, J >> 1), (0x4040, 0),
                    (0x403D, 1), (0x403D, 3), (0x403D, 5), (1, 1), (0, 1),
                    (0x3FFE, J), (0, 0), (0x403E, 2 * J - 1), (0x4040, (J >> 1) + 1))
        for exponent, significand in operands:
            for sign in range(2):
                for pc in range(4):
                    for rc in range(4):
                        raw = (exponent | sign << 15, significand)
                        add(f"FRNDINT {raw[0]:04x}:{raw[1]:016x} pc={pc} rc={rc}",
                            0xD9, 0xFC, raw, cw=0xBF | pc << 8 | rc << 10)
    if suite in ("overflow", "all"):
        for pc in (0, 2, 3):
            for rc in range(4):
                for sign in range(2):
                    a = (0x7FFE | sign << 15, 2 * J - 1)
                    for name, opcode, b in (("FADD", 0xC1, a),
                                             ("FSUB", 0xE1, (a[0] ^ 0x8000, a[1])),
                                             ("FMUL", 0xC9, integer(2)),
                                             ("FDIV", 0xF1, (0x3FFE, J))):
                        add(f"{name} masked overflow sign={sign} pc={pc} rc={rc}",
                            0xD8, opcode, a, b, cw=0x10BF | pc << 8 | rc << 10)
    if suite in ("constants", "all"):
        loads = ("FLD1", "FLDL2T", "FLDL2E", "FLDPI", "FLDLG2", "FLDLN2", "FLDZ")
        storage = tuple(integer(n) for n in range(11, 19))
        for index, name in enumerate(loads):
            for pc in (0, 2, 3):
                for rc in range(4):
                    for top in range(8):
                        for full in (False, True):
                            for sticky in (0, 32):
                                add(f"{name} pc={pc} rc={rc} top={top} full={full} stickyPE={bool(sticky)}",
                                    0xD9, 0xE8 + index, storage[0], storage[1],
                                    cw=0xBF | pc << 8 | rc << 10, sw=top << 11 | sticky,
                                    tags=0 if full else 0xFFFF, other=storage[2:])
    if suite in ("encodings", "all"):
        forms = ((0xD9, 0xD8, "FSTP"), (0xDC, 0xD0, "FCOM"),
                 (0xDC, 0xD8, "FCOMP"), (0xDD, 0xC8, "FXCH"),
                 (0xDE, 0xD0, "FCOMP"), (0xDF, 0xC0, "FFREE+pop"),
                 (0xDF, 0xC8, "FXCH"), (0xDF, 0xD0, "FSTP"), (0xDF, 0xD8, "FSTP"))
        for esc, base, name in forms:
            for i in range(8):
                add(f"Intel Table A-2 {esc:02x} {base+i:02x} {name} ST({i})", esc, base+i,
                    integer(1), integer(2), other=tuple(integer(k) for k in range(3, 9)))
    return records, names

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--suite", choices=("smoke", "research", "frndint", "overflow", "constants", "encodings", "all"), default="smoke")
    args = parser.parse_args()
    try:
        args.output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.exit(2, f"error: require a new campaign directory: {error}\n")
    records, names = make_cases(args.suite)
    dest = args.output / "VECTORS.BIN"
    write_trace(dest, INPUT_MAGIC, records)
    manifest = {"format": "i8087-vector-manifest-v1", "suite": args.suite,
                "input_sha256": hashlib.sha256(dest.read_bytes()).hexdigest(),
                "expected_results": "NONE: inputs only, not hardware evidence", "cases": names}
    (args.output / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    metadata = {"provenance": "UNFILLED_TEMPLATE_NOT_A_CAPTURE", "chip_marking": "",
                "silicon_revision_if_known": "", "board": "", "cpu": "", "clock_hz": None,
                "dos_version": "", "capture_date": "", "collector_sha256": "",
                "input_sha256": manifest["input_sha256"], "capture_sha256": "",
                "notes": "Record whether this was PHYSICAL hardware or an emulator; never label model output as hardware."}
    (args.output / "capture-metadata.template.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(records)} INPUT vectors ({args.suite}); no hardware results generated.")

if __name__ == "__main__":
    main()
