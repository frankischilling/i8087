#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare captured raw state against model output; never turn skips into passes."""
from __future__ import annotations
import argparse
import json
import struct
from pathlib import Path
from trace_format import INPUT_MAGIC, MODEL_MAGIC, HARDWARE_MAGIC, read_trace

def mask_for(esc: int, modrm: int, reference: bytes, all_bits: bool) -> bytes:
    mask = bytearray(b"\xff" * 94)
    if all_bits:
        return bytes(mask)
    # Architectural comparison, not a claim about undocumented/reserved bits.
    swmask = 0xB8BF  # BUSY, TOP, IR, exception flags; ordinary arithmetic CC unspecified
    if esc == 0xD9 and modrm == 0xE5:
        swmask = 0xFFBF  # FXAM defines all four CC bits
    elif esc == 0xD9 and modrm == 0xF8:
        status = struct.unpack_from("<H", reference, 2)[0]
        swmask = 0xBCBF if status & 0x400 else 0xFFBF
    elif (esc == 0xD8 and 0xD0 <= modrm <= 0xDF) or (esc == 0xD9 and modrm == 0xE4) or (esc == 0xDE and modrm == 0xD9):
        swmask = 0xFFBF
    struct.pack_into("<H", mask, 0, 0x1FBF)
    struct.pack_into("<H", mask, 2, swmask)
    struct.pack_into("<H", mask, 8, 0xF7FF)   # pointer bits + 11-bit opcode
    struct.pack_into("<H", mask, 12, 0xF000)  # data pointer's high nibble
    return bytes(mask)

def field(offset: int) -> str:
    if offset < 2: return "control"
    if offset < 4: return "status"
    if offset < 6: return "tags"
    if offset < 10: return "instruction_pointer/opcode"
    if offset < 14: return "data_pointer"
    return f"saved_ST{(offset - 14) // 10}"

def compare(vectors, reference, model, all_bits=False):
    if vectors.magic != INPUT_MAGIC or model.magic != MODEL_MAGIC:
        raise ValueError("require INPUT vectors and MODEL output in that order")
    if not len(vectors.records) == len(reference.records) == len(model.records):
        raise ValueError("record counts differ")
    if reference.instruction_address != model.instruction_address:
        raise ValueError("instruction addresses differ; replay with --reference hardware.bin")
    matched = blocked = approximated = mismatched = 0
    details = []
    for index, (query, hw, emu) in enumerate(zip(vectors.records, reference.records, model.records)):
        hw_result, hw_gaps = struct.unpack_from("<HI", hw)
        result, gaps = struct.unpack_from("<HI", emu)
        if hw_result or hw_gaps:
            raise ValueError(f"reference record {index} is not a completed hardware-style result")
        if result:
            blocked += 1
            details.append({"index": index, "classification": "blocked", "result": result, "gaps": gaps})
            continue
        if gaps:
            approximated += 1
        mask = mask_for(query[0], query[1], hw[6:], all_bits)
        offsets = [i for i, (a, b, m) in enumerate(zip(hw[6:], emu[6:], mask)) if (a ^ b) & m]
        if offsets:
            mismatched += 1
            details.append({"index": index, "classification": "mismatch", "gaps": gaps,
                            "fields": sorted({field(i) for i in offsets}),
                            "differences": [{"byte": i, "reference": hw[i+6], "model": emu[i+6], "mask": mask[i]}
                                            for i in offsets]})
        else:
            matched += 1
    return {"records": len(vectors.records), "matched": matched, "blocked": blocked,
            "approximated": approximated, "mismatched": mismatched,
            "mode": "all bits" if all_bits else "documented-field masks",
            "claim": "Dataset comparison only; not proof of complete 1:1 emulation", "details": details}

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vectors", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("model", type=Path)
    parser.add_argument("--all-bits", action="store_true")
    parser.add_argument("--self-test", action="store_true", help="allow MODEL reference; NEVER hardware evidence")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    try:
        vectors, reference, model = map(read_trace, (args.vectors, args.reference, args.model))
        expected = MODEL_MAGIC if args.self_test else HARDWARE_MAGIC
        if reference.magic != expected:
            raise ValueError("reference provenance mismatch; MODEL files are not hardware captures")
        report = compare(vectors, reference, model, args.all_bits)
        report["provenance"] = ("MODEL-ONLY SELF-TEST; no hardware evidence" if args.self_test else
                                "User-supplied capture file; preserve chip/board metadata and hashes separately")
        if args.json:
            args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({key: value for key, value in report.items() if key != "details"}, indent=2))
        for item in report["details"][:12]:
            print(json.dumps(item))
        raise SystemExit(1 if report["blocked"] or report["approximated"] or report["mismatched"] else 0)
    except (ValueError, OSError) as error:
        parser.exit(2, f"error: {error}\n")

if __name__ == "__main__":
    main()
