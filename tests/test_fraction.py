#!/usr/bin/env python3
"""Independent exact-rational oracle. Does NOT certify original 8087 hardware."""
from fractions import Fraction
from math import isqrt
from pathlib import Path
import random
import subprocess
import sys

J = 1 << 63
IE, OE, UE, PE = 1, 8, 16, 32

def value(pair):
    e, s = pair
    exponent = (e & 0x7fff or 1) - 16383 - 63
    x = Fraction(s << exponent) if exponent >= 0 else Fraction(s, 1 << -exponent)
    return -x if e & 0x8000 else x

def log2_floor(x):
    n, d = x.numerator, x.denominator
    e = n.bit_length() - d.bit_length()
    if (n < d << e) if e >= 0 else (n << -e < d):
        e -= 1
    return e

def scale(x, shift):
    return x * (1 << shift) if shift >= 0 else x / (1 << -shift)

def quantize(x, p, rc, emin=-16382, emax=16383, sqrt=False, zero_sign=0):
    if not x:
        return zero_sign << 15, 0, 0, False
    sign = x < 0
    x = abs(x)
    e = log2_floor(x)
    if sqrt:
        e //= 2
    tiny = e < emin
    e = max(e, emin)
    k = p - 1 - e
    if sqrt:
        y = scale(x, 2 * k)
        q = isqrt(y.numerator // y.denominator)
        exact = q * q * y.denominator == y.numerator
        twice = 4 * y.numerator - (2*q + 1)**2 * y.denominator
    else:
        y = scale(x, k)
        q, remainder = divmod(y.numerator, y.denominator)
        exact = not remainder
        twice = 2 * remainder - y.denominator
    up = not exact and ((rc == 0 and (twice > 0 or (twice == 0 and q & 1)))
                       or (rc == 1 and sign) or (rc == 2 and not sign))
    q += bool(up)
    if q >= 1 << p:
        q >>= 1
        e += 1
    flags = 0 if exact else PE
    if e > emax:
        inf = rc == 0 or (rc == 1 and sign) or (rc == 2 and not sign)
        return ((sign << 15) | (0x7fff if inf else emax + 16383),
                J if inf else ((1 << p)-1) << (64-p), flags | OE | PE, bool(up))
    sig = q << (64-p)
    if tiny and not exact and sig < J:
        flags |= UE
    encoded = (e+16383) if sig and not (e == -16382 and sig < J) else 0
    return (sign << 15) | encoded, sig, flags, bool(up)

def round_magnitude(x, rc):
    sign = x < 0
    y = abs(x)
    q, rem = divmod(y.numerator, y.denominator)
    up = bool(rem and ((rc == 0 and (2*rem > y.denominator or
                                   (2*rem == y.denominator and q & 1))) or
                      (rc == 1 and sign) or (rc == 2 and not sign)))
    return q + up, bool(rem), up

def store_expected(pair, width, rc):
    frac, bias = (23,127) if width == 32 else (52,1023)
    x = value(pair)
    sign = pair[0] >> 15
    mask = (1 << (8 if width == 32 else 11)) - 1
    if not x:
        return 0, sign << (width-1), 0, False
    e, sig, flags, up = quantize(x, frac+1, rc, 1-bias, bias)
    if e & 0x7fff == 0x7fff:
        bits = mask << frac
    else:
        expfield = (e & 0x7fff) - 16383 + bias if sig & J else 0
        bits = (expfield << frac) | ((sig >> (63-frac)) & ((1 << frac)-1))
    return 0, bits | (sign << (width-1)), flags, up

def integer_expected(pair, width, rc):
    x = value(pair)
    mag, lost, up = round_magnitude(x, rc)
    result = -mag if x < 0 else mag
    if result < -(1 << (width-1)) or result >= 1 << (width-1):
        return 0, 1 << (width-1), IE, False
    return 0, result & ((1 << 64)-1), PE if lost else 0, up

def rounded_expected(pair, rc):
    x = value(pair)
    mag, lost, up = round_magnitude(x, rc)
    result = -mag if x < 0 else mag
    e, sig, _, _ = quantize(Fraction(result),64,0,zero_sign=pair[0]>>15)
    return e, sig, PE if lost else 0, up

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_fraction.py /path/to/i8087_numeric_cli")
    rng = random.Random(0x80871980)
    vectors = []
    # Values immediately around precision transitions, cancellation and extremes.
    specials = [(0,0),(0x8000,0),(0x3fff,J),(0xbfff,J),(0x3fff,J+1),
                (0x3ffe,2*J-1),(1,J),(0,1),(0,J-1),(0x7ffe,2*J-1)]
    for _ in range(14000):
        op = rng.choice(("add", "sub", "mul", "div", "sqrt"))
        p, rc = rng.choice((24,53,64)), rng.randrange(4)
        def operand():
            if rng.randrange(5) == 0:
                return rng.choice(specials)
            e = rng.choice((rng.randrange(1,0x7fff), rng.randrange(16300,16460)))
            return (e | (rng.randrange(2)<<15), J | rng.getrandbits(63))
        a, b = operand(), operand()
        if op == "sqrt":
            a = (a[0] & 0x7fff, a[1])
        if op == "div" and not b[1]:
            b = (0x3fff,J)
        av, bv = value(a), value(b)
        sign = 0
        if op in ("add","sub"):
            if op == "sub": bv = -bv
            x = av + bv
            if not x:
                sa, sb = a[0]>>15, (b[0]>>15) ^ (op == "sub")
                sign = sa if not av and not bv and sa == sb else rc == 1
        elif op == "mul":
            x = av * bv; sign = (a[0]^b[0])>>15
        elif op == "div":
            x = av / bv; sign = (a[0]^b[0])>>15
        else:
            x = av; sign = a[0]>>15
        expected = quantize(x,p,rc,sqrt=op=="sqrt",zero_sign=sign)
        vectors.append((f"{op} {p} {rc} {a[0]:x} {a[1]:x} {b[0]:x} {b[1]:x}",expected))
    # Conversions are checked independently, including signed integer limits,
    # ties, exact/subnormal IEEE values, huge overflow and tiny underflow.
    for _ in range(12000):
        op = rng.choice(("store32","store64","int16","int32","int64","round"))
        rc = rng.randrange(4)
        if rng.randrange(6) == 0:
            a = rng.choice(specials)
        else:
            exponents = [rng.randrange(1,0x7fff),16383+rng.randrange(-10,67),
                         16383+rng.randrange(-1080,-1015),16383+rng.randrange(-153,-122)]
            a = (rng.choice(exponents) | (rng.randrange(2)<<15), J|rng.getrandbits(63))
        if op.startswith("store"):
            expected = store_expected(a,int(op[5:]),rc)
        elif op.startswith("int"):
            expected = integer_expected(a,int(op[3:]),rc)
        else:
            expected = rounded_expected(a,rc)
        vectors.append((f"{op} 64 {rc} {a[0]:x} {a[1]:x} 0 0",expected))
    # Explicit ties in all precisions, signs, and rounding directions.
    for p in (24,53,64):
        for rc in range(4):
            for sign in range(2):
                for low in (0,1):
                    a=(0x3fff | (sign << 15),J + (low << (64-p)))
                    b=(16383-p | (sign << 15),J)
                    expected=quantize(value(a)+value(b),p,rc)
                    vectors.append((f"add {p} {rc} {a[0]:x} {a[1]:x} {b[0]:x} {b[1]:x}",expected))
    text = "\n".join(v[0] for v in vectors)+"\n"
    proc = subprocess.run([sys.argv[1]],input=text,text=True,capture_output=True,check=True)
    lines = proc.stdout.splitlines()
    assert len(lines)==len(vectors), (len(lines),len(vectors))
    for i,(line,(query,expected)) in enumerate(zip(lines,vectors)):
        fields=line.split()
        got=(int(fields[0],16),int(fields[1],16),int(fields[2],16),bool(int(fields[3])))
        if got != expected:
            raise AssertionError(f"case {i}: {query}\n got {got}\nwant {expected}")
    print(f"PASS: {len(vectors)} independent exact-rational arithmetic, sqrt, conversion and rounding cases")
if __name__ == "__main__":
    main()
