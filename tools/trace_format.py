#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Portable trace encoding; bytes marked MODEL are never hardware captures."""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Iterable

HEADER = struct.Struct("<8sIHHI12s")
INPUT_MAGIC, MODEL_MAGIC, HARDWARE_MAGIC = b"I87V0001", b"I87M0001", b"I87H0001"
INPUT_SIZE, OUTPUT_SIZE = 96, 100
MAX_RECORDS = 1_000_000

@dataclass(frozen=True)
class Trace:
    magic: bytes
    instruction_address: int
    records: tuple[bytes, ...]

def read_trace(path: str | Path) -> Trace:
    path = Path(path)
    with path.open("rb") as stream:
        raw = stream.read(HEADER.size)
        if len(raw) != HEADER.size:
            raise ValueError(f"{path}: truncated header")
        magic, count, size, flags, ip, reserved = HEADER.unpack(raw)
        expected = INPUT_SIZE if magic == INPUT_MAGIC else OUTPUT_SIZE
        if magic not in (INPUT_MAGIC, MODEL_MAGIC, HARDWARE_MAGIC):
            raise ValueError(f"{path}: unsupported provenance/version marker")
        if not 1 <= count <= MAX_RECORDS or size != expected or flags or any(reserved) or ip > 0xFFFFF:
            raise ValueError(f"{path}: invalid header fields")
        if path.stat().st_size != HEADER.size + size * count:
            raise ValueError(f"{path}: size/count mismatch (truncated or trailing data)")
        records = tuple(stream.read(size) for _ in range(count))
    return Trace(magic, ip, records)

def write_trace(path: str | Path, magic: bytes, records: Iterable[bytes], ip: int = 0x100) -> None:
    records = tuple(records)
    size = INPUT_SIZE if magic == INPUT_MAGIC else OUTPUT_SIZE
    if magic not in (INPUT_MAGIC, MODEL_MAGIC) or not 0 <= ip <= 0xFFFFF:
        raise ValueError("Python writer only emits INPUT or MODEL provenance")
    if not 1 <= len(records) <= MAX_RECORDS or any(len(r) != size for r in records):
        raise ValueError("wrong record size or excessive count")
    Path(path).write_bytes(HEADER.pack(magic, len(records), size, 0, ip, bytes(12)) + b"".join(records))

def tag(pair: tuple[int, int]) -> int:
    e, sig = pair
    exponent = e & 0x7FFF
    if exponent == 0x7FFF or (exponent == 0 and sig):
        return 2
    if exponent == 0 and sig == 0:
        return 1
    return 0  # includes original-8087 unnormals and pseudozeros

def vector(esc: int, modrm: int, a: tuple[int, int], b: tuple[int, int] | None = None,
           cw: int = 0x3BF, sw: int = 0, tags: int | None = None,
           other: tuple[tuple[int, int], ...] = ()) -> bytes:
    """Pairs use (sign/exponent, significand) in logical ST order; tags are physical."""
    top = (sw >> 11) & 7
    values = [a] + ([b] if b is not None else []) + list(other)
    if len(values) > 8:
        raise ValueError("more than eight registers")
    derived = 0xFFFF
    for i, pair in enumerate(values):
        physical = (top + i) & 7
        derived = (derived & ~(3 << (2 * physical))) | (tag(pair) << (2 * physical))
    values += [(0, 0)] * (8 - len(values))
    image = struct.pack("<7H", cw, sw, derived if tags is None else tags, 0, 0, 0, 0)
    image += b"".join(struct.pack("<QH", sig, exp) for exp, sig in values)
    return bytes((esc, modrm)) + image

def outcome(record: bytes) -> tuple[int, int, int, int, tuple[tuple[int, int], ...]]:
    if len(record) != OUTPUT_SIZE:
        raise ValueError("wrong output size")
    result, gaps, cw, sw, tags = struct.unpack_from("<HIHHH", record)
    del cw
    registers = tuple((exp, sig) for sig, exp in
                      (struct.unpack_from("<QH", record, 20 + 10 * i) for i in range(8)))
    return result, gaps, sw, tags, registers
