#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent rational checks of encoded instructions, not hardware vectors."""
from fractions import Fraction
from pathlib import Path
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from trace_format import INPUT_MAGIC, MODEL_MAGIC, outcome, read_trace, vector, write_trace
from test_fraction import value, quantize, log2_floor, scale

J = 1 << 63
C0, C1, C2, C3, PE, UE, OE = 0x100, 0x200, 0x400, 0x4000, 32, 16, 8

def qflags(q):
    return (C0 if q & 4 else 0) | (C3 if q & 2 else 0) | (C1 if q & 1 else 0)

def unbounded_round(x, precision, rc):
    """Return exponent/significand without imposing binary80 exponent limits."""
    sign = x < 0
    exact_exp = log2_floor(abs(x))
    y = scale(abs(x), precision - 1 - exact_exp)
    q, remainder = divmod(y.numerator, y.denominator)
    increment = bool(remainder and
                     ((rc == 0 and (2 * remainder > y.denominator or
                                    (2 * remainder == y.denominator and q & 1))) or
                      (rc == 1 and sign) or (rc == 2 and not sign)))
    q += increment
    exponent = exact_exp
    if q == 1 << precision:
        q >>= 1
        exponent += 1
    return sign, exact_exp, exponent, q << (64 - precision), PE if remainder else 0

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_instruction_fraction.py /path/to/i8087_replay")
    rng = random.Random(0x80872026)
    inputs, expectations = [], []
    # Fractional remainders, original quotient shift history, sign, and unnormal dividends.
    for _ in range(6500):
        be = rng.randrange(15500, 17200)
        ae = be + rng.randrange(-80, 64)
        a = (ae | rng.randrange(2) << 15, J | rng.getrandbits(63))
        if rng.randrange(4) == 0:
            a = (a[0], a[1] >> rng.randrange(1, 64))
        b = (be | rng.randrange(2) << 15, J | rng.getrandbits(63))
        av, bv = value(a), value(b)
        q = abs(av) // abs(bv)
        remainder = av - (-1 if av < 0 else 1) * q * abs(bv)
        exp, sig, flags, _ = quantize(remainder, 64, 0, zero_sign=a[0] >> 15)
        assert flags == 0
        old = rng.randrange(8)
        width = max(1, q.bit_length())
        bits = ((old << width) | q) & 7
        cw = 0xBF | rng.randrange(4) << 8 | rng.randrange(4) << 10
        inputs.append(vector(0xD9, 0xF8, a, b, cw, qflags(old) | C2))
        expectations.append((0, (exp, sig), 0, qflags(bits), "FPREM"))
    # Values near both binary80 range boundaries: before, at, and past the boundary.
    operations = {"add": 0xC1, "sub": 0xE1, "mul": 0xC9, "div": 0xF1}
    for _ in range(10000):
        op = rng.choice(tuple(operations))
        p, pc = rng.choice(((24, 0), (53, 2), (64, 3)))
        rc = rng.randrange(4)
        ae = rng.choice((1, 2, 3, 0x7FFC, 0x7FFD, 0x7FFE, 0x3FFF))
        be = rng.choice((1, 2, 3, 0x7FFD, 0x7FFE, 0x3FFE, 0x3FFF, 0x4000))
        a = (ae | rng.randrange(2) << 15, J | rng.getrandbits(63))
        b = (be | rng.randrange(2) << 15, J | rng.getrandbits(63))
        av, bv = value(a), value(b)
        x = {"add": lambda: av + bv, "sub": lambda: av - bv,
             "mul": lambda: av * bv, "div": lambda: av / bv}[op]()
        assert x
        sign, exact_exp, exponent, sig, flags = unbounded_round(x, p, rc)
        if exponent > 16383:
            exponent -= 24576
            flags |= OE
        elif exact_exp < -16382:
            exponent += 24576
            flags |= UE
        assert -16382 <= exponent <= 16383
        expected = (int(sign) << 15 | exponent + 16383, sig)
        cw = (0xBF | pc << 8 | rc << 10) & ~(OE | UE)
        if rng.randrange(2):
            cw &= ~PE
        result = 1 if flags & ~cw & 0x3F else 0
        inputs.append(vector(0xD8, operations[op], a, b, cw))
        expectations.append((result, expected, flags, None, f"{op} unmasked range p={p} rc={rc}"))
    # Original masked, exact-tiny underflow signals UE even without precision loss.
    for _ in range(2500):
        rc = rng.randrange(4)
        a = (1 | rng.randrange(2) << 15, J | rng.getrandbits(63))
        b = (16383 - rng.randrange(1, 100) | rng.randrange(2) << 15, J | rng.getrandbits(63))
        x = value(a) * value(b)
        e, sig, flags, _ = quantize(x, 64, rc)
        if log2_floor(abs(x)) < -16382:
            flags |= UE
        inputs.append(vector(0xD8, 0xC9, a, b, 0x3BF | rc << 10))
        expectations.append((0, (e, sig), flags, None, "masked register underflow"))
    # Intel S-75: directed masked overflow raises PE without adding sticky OE.
    # The finite register bound is constructed after PC rounding at full width.
    for _ in range(2000):
        op = rng.choice(tuple(operations))
        p, pc = rng.choice(((24, 0), (53, 2), (64, 3)))
        rc, sign = rng.choice((1, 2)), rng.randrange(2)
        a = (0x7FFE | sign << 15, J | rng.getrandbits(63))
        be = 0x4000 if op == "mul" else 0x3FFD if op == "div" else 0x7FFE
        bsign = (sign ^ (op == "sub")) if op in ("add", "sub") else 0
        b = (be | bsign << 15, J | rng.getrandbits(63))
        av, bv = value(a), value(b)
        x = {"add": lambda: av + bv, "sub": lambda: av - bv,
             "mul": lambda: av * bv, "div": lambda: av / bv}[op]()
        _, _, exponent, _, _ = unbounded_round(x, p, rc)
        assert exponent > 16383 and (x < 0) == bool(sign)
        finite = (sign and rc == 2) or (not sign and rc == 1)
        expected = ((sign << 15) | (0x7FFE if finite else 0x7FFF), 2 * J - 1 if finite else J)
        sticky = OE if rng.randrange(2) else 0
        cw = 0xBF | pc << 8 | rc << 10
        if rng.randrange(2):
            cw &= ~PE
        inputs.append(vector(0xD8, operations[op], a, b, cw, sticky))
        expectations.append((1 if not cw & PE else 0, expected, PE | sticky, None,
                             f"masked directed overflow {op} p={p} rc={rc}"))
    with tempfile.TemporaryDirectory() as directory:
        src, dst = Path(directory) / "vectors.bin", Path(directory) / "model.bin"
        write_trace(src, INPUT_MAGIC, inputs)
        subprocess.run([sys.argv[1], str(src), str(dst)], check=True, capture_output=True, text=True)
        trace = read_trace(dst)
        assert trace.magic == MODEL_MAGIC and len(trace.records) == len(inputs)
        for index, (raw, expect) in enumerate(zip(trace.records, expectations)):
            result, gaps, sw, tags, registers = outcome(raw)
            want_result, want_value, flags, cc, label = expect
            if (result, gaps, registers[0], sw & 0x3F) != (want_result, 0, want_value, flags):
                raise AssertionError(f"case {index} ({label}) input={inputs[index].hex()}\n"
                                     f"got result={result} gaps={gaps:x} st0={registers[0]} flags={sw & 0x3f:x}\n"
                                     f"want {expect}")
            assert sw & 0x3800 == 0, index
            assert bool(sw & 0x80) == (result == 1), index
            e, sig = want_value
            exponent = e & 0x7FFF
            tag = 2 if exponent == 0x7FFF or (exponent == 0 and sig) else 1 if not sig else 0
            assert tags & 3 == tag, (index, label, tags, tag)
            if cc is not None:
                assert sw & 0x4700 == cc, (index, label, sw, cc)
    print(f"PASS: {len(inputs)} independent Fraction instruction-level remainder/range cases")

if __name__ == "__main__":
    main()
