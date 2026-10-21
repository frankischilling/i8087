#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent integer divmod checks of original stored-exponent reductions."""
from fractions import Fraction
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from trace_format import INPUT_MAGIC, MODEL_MAGIC, outcome, read_trace, vector, write_trace

J = 1 << 63
CC = 0x4700


def condition_bits(history):
    return ((history & 4) << 6) | ((history & 2) << 13) | ((history & 1) << 9)


def expected(a, b, status):
    ae, be = a[0] & 0x7FFF, b[0] & 0x7FFF
    distance = ae - be
    partial = distance >= 64
    count = min(max(distance, 0), 63)
    quotient, remainder = divmod(a[1] << count, b[1]) if distance >= 0 else (0, a[1])
    exponent = ae - count
    if remainder:
        shift = 64 - remainder.bit_length()
        remainder <<= shift
        exponent -= shift
        assert 1 <= exponent <= 0x7FFE
    else:
        exponent = 0
    result = (exponent | (a[0] & 0x8000), remainder)
    old = (4 if status & 0x100 else 0) | (2 if status & 0x4000 else 0) | (1 if status & 0x200 else 0)
    guard = partial and quotient < 4
    history = old if guard else ((old << max(1, quotient.bit_length())) | quotient) & 7
    final_status = (status & ~CC) | condition_bits(history) | (0x400 if partial else 0)
    return result, final_status, guard


def numeric(raw):
    exponent, fraction = raw
    shift = (exponent & 0x7FFF) - 16383 - 63
    return fraction * (Fraction(2) ** shift) * (-1 if exponent & 0x8000 else 1)


def main():
    rng = random.Random(0x8087F8)
    inputs, predictions = [], []
    sentinels = tuple((0x4002, J + i) for i in range(6))

    def append(a, b, control, status):
        query = vector(0xD9, 0xF8, a, b, control, status, other=sentinels)
        inputs.append(query)
        result, final_status, guard = expected(a, b, status)
        predictions.append((result, final_status, guard))
        return result, final_status, guard

    for _ in range(30000):
        distance = rng.choice((-80, -1, 0, 1, 2, 62, 63, 64, 65, 66, 127, 128,
                               32700, rng.randrange(-80, 32701)))
        be = max(0x41, 0x41 - distance)
        ae = be + distance
        assert 1 <= ae <= 0x7FFE and 1 <= be <= 0x7FFE
        a_fraction = rng.choice((0, 1, 2, 3, 4, 7, 8, J - 1, J, J + 1, 2 * J - 1,
                                 rng.getrandbits(64) or 1))
        b_fraction = rng.choice((J, J + 1, J + 3, 3 * J // 2, 2 * J - 1,
                                 J | rng.getrandbits(63)))
        a = (ae | rng.randrange(2) << 15, a_fraction)
        b = (be | rng.randrange(2) << 15, b_fraction)
        status = (rng.randrange(8) << 11 | condition_bits(rng.randrange(8)) |
                  (0x400 if rng.randrange(2) else 0) | rng.randrange(64))
        control = 0x1000 | rng.randrange(4) << 8 | rng.randrange(4) << 10 | rng.choice((0, 0x3F))
        append(a, b, control, status)

    sequences = 128
    for _ in range(sequences):
        distance = rng.choice((64, 128, 257, 1000, 32700))
        a = (0x41 + distance | rng.randrange(2) << 15, J | rng.getrandbits(63))
        b = (0x41 | rng.randrange(2) << 15, J | rng.getrandbits(63))
        initial_a = a
        initial_quotient = abs(numeric(a)) // abs(numeric(b))
        final_value = abs(numeric(a)) - initial_quotient * abs(numeric(b))
        status = rng.randrange(8) << 11 | condition_bits(rng.randrange(8)) | rng.randrange(64)
        control = 0x3F | rng.randrange(4) << 8 | rng.randrange(4) << 10
        steps = 0
        while True:
            # A canonical zero bypasses the finite-nonzero subsystem. Model
            # its documented history step separately from a divmod call.
            if not a[1]:
                query = vector(0xD9, 0xF8, a, b, control, status, other=sentinels)
                inputs.append(query)
                history = (4 if status & 0x100 else 0) | (2 if status & 0x4000 else 0) | (1 if status & 0x200 else 0)
                status = (status & ~CC) | condition_bits((history << 1) & 7)
                predictions.append((a, status, False))
                break
            previous_exponent = a[0] & 0x7FFF
            a, status, guard = append(a, b, control, status)
            assert not guard
            steps += 1
            if not status & 0x400:
                break
            assert not a[1] or (a[0] & 0x7FFF) <= previous_exponent - 63
            assert steps <= distance // 63 + 2
        assert abs(numeric(a)) == final_value, (initial_a, b, a)

    guarded = sum(prediction[2] for prediction in predictions)
    assert guarded > 1000 and len(inputs) > 30000
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "input.bin"
        write_trace(source, INPUT_MAGIC, inputs)
        for approximate in (False, True):
            destination = Path(directory) / f"model-{approximate}.bin"
            command = [sys.argv[1], str(source), str(destination)]
            if approximate:
                command.append("--approximate")
            subprocess.run(command, check=True, capture_output=True, text=True)
            trace = read_trace(destination)
            assert trace.magic == MODEL_MAGIC and len(trace.records) == len(inputs)
            for index, (query, raw, prediction) in enumerate(zip(inputs, trace.records, predictions)):
                control, status, tags = struct.unpack_from("<HHH", query, 2)
                registers = tuple((e, s) for s, e in struct.iter_unpack("<QH", query[16:]))
                result, final_status, guard = prediction
                blocked = guard and not approximate
                expected_registers = registers if blocked else (result,) + registers[1:]
                if not blocked:
                    top = (status >> 11) & 7
                    tags = (tags & ~(3 << (2 * top))) | ((0 if result[1] else 1) << (2 * top))
                want = (2 if blocked else 0, 16 if guard else 0,
                        status if blocked else final_status, tags, expected_registers)
                got = outcome(raw)
                assert got == want, (index, approximate, query.hex(), got, want)
                assert struct.unpack_from("<H", raw, 6)[0] == control
    print(f"PASS: {len(inputs)} integer remainder states, {sequences} complete sequences, "
          f"{guarded} partial-history guards; strict and opt-in replay; no chip captures")


if __name__ == "__main__":
    main()
