#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Source-derived raw-alignment checks using exact rational arithmetic."""
from fractions import Fraction
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from trace_format import INPUT_MAGIC, MODEL_MAGIC, outcome, read_trace, vector, write_trace
from make_vectors import make_cases

J = 1 << 63


def expected(a, b, modrm, precision, rc):
    """Choose the source-prescribed alignment, then round an exact Fraction."""
    ae, be = a[0] & 0x7FFF, b[0] & 0x7FFF
    exponent = max(ae, be)
    av = Fraction(a[1], 2 ** (exponent - ae)) * (-1 if a[0] & 0x8000 else 1)
    bv = Fraction(b[1], 2 ** (exponent - be)) * (-1 if b[0] & 0x8000 else 1)
    if modrm == 0xE1:
        bv = -bv
    elif modrm == 0xE9:
        av = -av
    total = av + bv
    if not total:
        larger_fraction = a[1] if ae >= be else b[1]
        zero_exponent = 0 if larger_fraction >= J else exponent
        return (zero_exponent | (0x8000 if rc == 1 else 0), 0), False, None
    magnitude = abs(total)
    if (av < 0) == (bv < 0):
        if magnitude >= 2 * J:
            magnitude /= 2
            exponent += 1
    else:
        # A normal significand survives in the larger-exponent position, or
        # an equal-exponent operand has an integer bit. Otherwise retain E.
        normalize = ((ae >= be and a[1] >= J) or (be >= ae and b[1] >= J))
        if normalize:
            while magnitude < J:
                magnitude *= 2
                exponent -= 1
    unit = 2 ** (64 - precision)
    grid = magnitude / unit * (-1 if total < 0 else 1)
    rounded = (round(grid) if rc == 0 else grid // 1 if rc == 1 else
               -((-grid) // 1) if rc == 2 else int(grid))
    inexact = rounded != grid
    increment = abs(rounded) > abs(grid)
    fraction = abs(rounded) * unit
    if fraction >= 2 * J:
        fraction //= 2
        exponent += 1
    assert 1 <= exponent <= 0x7FFE
    return (exponent | (0x8000 if total < 0 else 0), fraction), inexact, increment


def main():
    rng = random.Random(0x8087ADD)
    inputs, expectations = [], []
    while len(inputs) < 30000:
        distance = rng.choice((0, 0, 1, 1, 2, 3, 31, 62, 63, 64, 65, 66, 127,
                               32700, rng.randrange(32701)))
        larger = rng.choice((0x0041 + distance, max(0x0041 + distance, 0x4000), 0x7FFD))
        ae, be = larger, larger - distance
        if rng.randrange(2):
            ae, be = be, ae
        fractions = (1, 2, 3, J - 1, J // 2, J // 2 + 1, J,
                     J + 1, 2 * J - 1, rng.getrandbits(64) or 1)
        sa, sb = rng.choice(fractions), rng.choice(fractions)
        if sa >= J and sb >= J:
            sa //= 2
        a = (ae | rng.randrange(2) << 15, sa)
        b = (be | rng.randrange(2) << 15, sb)
        precision, pc = rng.choice(((24, 0), (53, 2), (64, 3)))
        rc, modrm = rng.randrange(4), rng.choice((0xC1, 0xE1, 0xE9))
        prediction = expected(a, b, modrm, precision, rc)
        result, inexact, increment = prediction
        masked = rng.randrange(2)
        control = 0x1000 | pc << 8 | rc << 10 | (0x3F if masked else 0)
        initial_status = rng.randrange(8) << 11 | rng.randrange(64)
        for bit in (0x100, 0x200, 0x400, 0x4000):
            if rng.randrange(2):
                initial_status |= bit
        sentinels = tuple((0x4002, J + i) for i in range(6))
        inputs.append(vector(0xD8, modrm, a, b, control, initial_status, other=sentinels))
        interrupt = inexact and not masked
        c1 = initial_status & 0x200 if increment is None else (0x200 if increment else 0)
        status = ((initial_status & ~0x200) | (0x20 if inexact else 0) |
                  c1 | (0x8080 if interrupt else 0))
        expectations.append((1 if interrupt else 0, 0, status, (result, b) + sentinels, 0))
    random_count = len(inputs)
    campaign, labels = make_cases("finite-add")
    assert len(campaign) == len(labels) == 46080
    for query in campaign:
        control, initial_status, tags = struct.unpack_from("<HHH", query, 2)
        registers = tuple((e, s) for s, e in struct.iter_unpack("<QH", query[16:]))
        a, b = registers[:2]
        precision = (24, None, 53, 64)[(control >> 8) & 3]
        rc = (control >> 10) & 3
        prediction = expected(a, b, query[1], precision, rc)
        inputs.append(query)
        result, inexact, increment = prediction
        c1 = initial_status & 0x200 if increment is None else (0x200 if increment else 0)
        status = (initial_status & ~0x200) | (0x20 if inexact else 0) | c1
        expectations.append((0, 0, status, (result,) + registers[1:], tags))
    with tempfile.TemporaryDirectory() as directory:
        source, destination = Path(directory) / "input.bin", Path(directory) / "model.bin"
        write_trace(source, INPUT_MAGIC, inputs)
        subprocess.run([sys.argv[1], str(source), str(destination)], check=True,
                       capture_output=True, text=True)
        trace = read_trace(destination)
        assert trace.magic == MODEL_MAGIC and len(trace.records) == len(inputs)
        for index, (query, raw, want) in enumerate(zip(inputs, trace.records, expectations)):
            result, gaps, status, tags, registers = outcome(raw)
            control, = struct.unpack_from("<H", raw, 6)
            initial_control, = struct.unpack_from("<H", query, 2)
            if (result, gaps, status, registers, tags) != want:
                raise AssertionError(f"case {index} input={query.hex()}\n"
                                     f"got {(result, gaps, status, registers, tags)}\nwant {want}")
            assert control == initial_control, (index, control, initial_control)
    print(f"PASS: {random_count} Fraction raw-add/subtract cases; "
          f"{len(campaign)} campaign inputs (all source-derived); no hardware evidence")


if __name__ == "__main__":
    main()
