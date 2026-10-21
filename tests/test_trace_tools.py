#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parser/provenance round trips, NOT hardware-accuracy tests."""
from pathlib import Path
from fractions import Fraction
import subprocess
import struct
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from trace_format import INPUT_MAGIC, MODEL_MAGIC, HARDWARE_MAGIC, HEADER, Trace, outcome, read_trace, vector, write_trace
from make_vectors import make_cases
from compare_traces import compare

def main():
    replay = sys.argv[1]
    tests = 0
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        src, dst = directory / "input.bin", directory / "model.bin"
        records, _ = make_cases("smoke")
        write_trace(src, INPUT_MAGIC, records)
        subprocess.run([replay, str(src), str(dst)], check=True, capture_output=True)
        inp, out = read_trace(src), read_trace(dst)
        assert out.magic == MODEL_MAGIC
        report = compare(inp, out, out, True)
        assert report["matched"] == len(records) and not report["blocked"] and not report["approximated"]
        tests += 1
        # Model provenance cannot be silently passed off as a hardware capture.
        cmd = [sys.executable, str(Path(__file__).resolve().parents[1]/"tools"/"compare_traces.py"), str(src), str(dst), str(dst)]
        assert subprocess.run(cmd, capture_output=True).returncode == 2
        tests += 1
        assert subprocess.run(cmd + ["--self-test", "--all-bits"], capture_output=True).returncode == 0
        tests += 1
        good = src.read_bytes()
        for broken in (b"", good[:31], good[:-1], good+b"x", b"NOTVALID"+good[8:]):
            src.write_bytes(broken)
            bad_output = directory / f"malformed-{tests}.bin"
            assert subprocess.run([replay, str(src), str(bad_output)], capture_output=True).returncode != 0
            try:
                read_trace(src)
            except ValueError:
                pass
            else:
                raise AssertionError("Python reader accepted malformed trace")
            tests += 1
        # An empty dataset is not accepted as a vacuous validation pass.
        header = bytearray(good[:HEADER.size]);header[8:12]=bytes(4)
        src.write_bytes(header)
        assert subprocess.run([replay, str(src), str(directory / "empty-output.bin")], capture_output=True).returncode != 0
        try:
            read_trace(src)
        except ValueError:
            pass
        else:
            raise AssertionError("empty dataset accepted")
        tests += 1
        # Larger count with no payload and unsupported record opcode are rejected.
        header = bytearray(good[:HEADER.size]);header[8:12]=(1_000_001).to_bytes(4,"little")
        src.write_bytes(header)
        assert subprocess.run([replay, str(src), str(directory / "count-output.bin")], capture_output=True).returncode != 0
        tests += 1
        bad = bytearray(records[0]);bad[0:2]=b"\xdf\xe0" # modern FNSTSW AX
        write_trace(src, INPUT_MAGIC, [bytes(bad)])
        assert subprocess.run([replay, str(src), str(directory / "opcode-output.bin")], capture_output=True).returncode != 0
        tests += 1
        # Research gaps are retained as blocked, not hidden in a passing tally.
        research, _ = make_cases("research")
        write_trace(src, INPUT_MAGIC, research)
        research_dst = directory / "research-model.bin"
        subprocess.run([replay, str(src), str(research_dst)], check=True, capture_output=True)
        results=read_trace(research_dst)
        assert any(int.from_bytes(r[:2],"little")==2 for r in results.records)
        tests += 1
        # Alternate spellings of the input must not become destructive outputs.
        write_trace(src, INPUT_MAGIC, records[:1])
        preserved = src.read_bytes()
        alias = str(src.parent) + "/./" + src.name
        aliased = subprocess.run([replay, str(src), alias], capture_output=True)
        assert src.read_bytes() == preserved, "replay overwrote its input through a path alias"
        assert aliased.returncode != 0
        tests += 1
        existing = directory / "existing-output.bin"
        existing.write_bytes(b"preserved previous result")
        assert subprocess.run([replay, str(src), str(existing)], capture_output=True).returncode != 0
        assert existing.read_bytes() == b"preserved previous result"
        tests += 1
        linked = directory / "input-hardlink.bin"
        linked.hardlink_to(src)
        assert subprocess.run([replay, str(src), str(linked)], capture_output=True).returncode != 0
        assert src.read_bytes() == linked.read_bytes() == preserved
        tests += 1
        # Synthetic framing fixture only; no bytes here came from hardware.
        reference = directory / "synthetic-reference.bin"
        reference_bytes = HEADER.pack(HARDWARE_MAGIC, 1, 100, 0, 0x12345, bytes(12)) + bytes(100)
        reference.write_bytes(reference_bytes)
        ref_alias = str(reference.parent) + "/./" + reference.name
        rejected = subprocess.run([replay, str(src), ref_alias, "--reference", str(reference)], capture_output=True)
        assert reference.read_bytes() == reference_bytes, "replay overwrote its reference"
        assert rejected.returncode != 0
        tests += 1
        for broken in (reference_bytes[:32], reference_bytes[:-1], reference_bytes + b"x"):
            reference.write_bytes(broken)
            fresh_output = directory / f"bad-reference-output-{tests}.bin"
            rejected = subprocess.run([replay, str(src), str(fresh_output), "--reference", str(reference)], capture_output=True)
            assert rejected.returncode != 0, "replay accepted an incomplete or trailing reference"
            assert not fresh_output.exists(), "invalid reference created output"
            assert reference.read_bytes() == broken and src.read_bytes() == preserved
            tests += 1
        reference.write_bytes(reference_bytes)
        fresh_output = directory / "fresh-model.bin"
        subprocess.run([replay, str(src), str(fresh_output), "--reference", str(reference)], check=True, capture_output=True)
        assert read_trace(fresh_output).instruction_address == 0x12345
        assert src.read_bytes() == preserved and reference.read_bytes() == reference_bytes
        tests += 1
        # Synthetic capture framing tests report output safety, not a processor.
        capture = directory / "synthetic-capture.bin"
        capture.write_bytes(HARDWARE_MAGIC + fresh_output.read_bytes()[8:])
        report_cmd = [sys.executable, str(Path(__file__).resolve().parents[1]/"tools"/"compare_traces.py"),
                      str(src), str(capture), str(fresh_output), "--json"]
        report_link = directory / "capture-hardlink.bin"
        report_link.hardlink_to(capture)
        old_report = directory / "old-report.json"
        old_report.write_bytes(b"preserved comparison report")
        protected = (src, capture, fresh_output, report_link, old_report)
        snapshots = {path: path.read_bytes() for path in protected}
        aliases = [str(path) for path in protected]
        aliases.append(str(capture.parent) + "/./" + capture.name)
        for target in aliases:
            rejected = subprocess.run(report_cmd + [target], capture_output=True)
            assert all(path.read_bytes() == data for path, data in snapshots.items()), \
                "comparison report overwrote preserved evidence"
            assert rejected.returncode == 2
            tests += 1
        report = directory / "fresh-report.json"
        assert subprocess.run(report_cmd + [str(report)], capture_output=True).returncode == 0
        assert report.is_file()
        tests += 1
        # Reserve a new campaign directory before writing any of its files.
        generator = [sys.executable, str(Path(__file__).resolve().parents[1]/"tools"/"make_vectors.py")]
        campaign = directory / "fresh-campaign"
        assert subprocess.run(generator + [str(campaign)], capture_output=True).returncode == 0
        generated = {path: path.read_bytes() for path in campaign.iterdir()}
        assert len(generated) == 3
        generated[campaign / "capture-metadata.template.json"] = b"preserved completed provenance"
        for path, data in generated.items():
            path.write_bytes(data)
        for destination in (str(campaign), str(campaign.parent) + "/./" + campaign.name):
            rejected = subprocess.run(generator + [destination, "--suite", "research"], capture_output=True)
            assert all(path.read_bytes() == data for path, data in generated.items()), \
                "generator replaced a preserved campaign"
            assert rejected.returncode == 2
            tests += 1
        partial = directory / "partial-campaign"
        partial.mkdir()
        metadata = partial / "capture-metadata.template.json"
        metadata.write_bytes(b"preserved partial campaign")
        assert subprocess.run(generator + [str(partial)], capture_output=True).returncode == 2
        assert metadata.read_bytes() == b"preserved partial campaign"
        assert list(partial.iterdir()) == [metadata]
        tests += 1
        empty = directory / "existing-empty-campaign"
        empty.mkdir()
        assert subprocess.run(generator + [str(empty)], capture_output=True).returncode == 2
        assert not list(empty.iterdir())
        tests += 1
        # Capture-ready masked inputs exercise generation/replay only here.
        rounding, labels = make_cases("frndint")
        assert len(rounding) == len(labels) == 384
        rounding_src, rounding_dst = directory / "rounding-input.bin", directory / "rounding-model.bin"
        write_trace(rounding_src, INPUT_MAGIC, rounding)
        subprocess.run([replay, str(rounding_src), str(rounding_dst)], check=True, capture_output=True)
        rounding_out = read_trace(rounding_dst)
        report = compare(read_trace(rounding_src), rounding_out, rounding_out, True)
        assert report["matched"] == 384 and not report["blocked"] and not report["approximated"]
        tests += 1
        overflow, labels = make_cases("overflow")
        assert len(overflow) == len(labels) == 96
        overflow_src, overflow_dst = directory / "overflow-input.bin", directory / "overflow-model.bin"
        write_trace(overflow_src, INPUT_MAGIC, overflow)
        subprocess.run([replay, str(overflow_src), str(overflow_dst)], check=True, capture_output=True)
        rows = read_trace(overflow_dst).records
        # CHOP remains unresolved; generation must not hide its guarded results.
        assert sum(int.from_bytes(row[:2], "little") == 2 for row in rows) == 24
        assert sum(int.from_bytes(row[:2], "little") == 0 for row in rows) == 72
        tests += 1
        # Saved values are logical; the tag word rotates with TOP.
        for top, expected_tags in ((0, 0xFFF4), (1, 0xFFD3), (7, 0x3FFD)):
            raw = vector(0xD9, 0xE8, (0x3FFF, 1 << 63), (0, 0), sw=top << 11)
            assert struct.unpack_from("<H", raw, 6)[0] == expected_tags
            assert struct.unpack_from("<H", raw, 4)[0] == top << 11
            tests += 1
        constants, labels = make_cases("constants")
        assert len(constants) == len(labels) == 2688
        constant_src, constant_dst = directory / "constant-input.bin", directory / "constant-model.bin"
        write_trace(constant_src, INPUT_MAGIC, constants)
        subprocess.run([replay, str(constant_src), str(constant_dst)], check=True, capture_output=True)
        passed = blocked = 0
        for query, raw in zip(constants, read_trace(constant_dst).records):
            result, gaps, sw, tags, registers = outcome(raw)
            initial_sw, initial_tags = struct.unpack_from("<HH", query, 4)
            top = (initial_sw >> 11) & 7
            full, candidate = initial_tags == 0, query[1] not in (0xE8, 0xEE)
            if full or candidate:
                assert result == 2 and gaps == ((256 if full else 0) | (32 if candidate else 0))
                assert raw[6:] == query[2:], "guarded constant load changed architectural state"
                blocked += 1
            else:
                new_top = (top + 7) & 7
                zero = query[1] == 0xEE
                assert result == gaps == 0
                assert registers[0] == ((0, 0) if zero else (0x3FFF, 1 << 63))
                assert (sw >> 11) & 7 == new_top and sw & 0x3F == initial_sw & 0x3F
                assert tags == ((0xFFFF & ~(3 << (2 * new_top))) | (int(zero) << (2 * new_top)))
                old_registers = tuple((e, s) for s, e in struct.iter_unpack("<QH", query[16:]))
                assert registers[1:] == old_registers[:7]
                passed += 1
        assert passed == 384 and blocked == 2304
        tests += 1
        alternate, labels = make_cases("encodings")
        assert len(alternate) == len(labels) == 72
        alternate_src, alternate_dst = directory / "alternate-input.bin", directory / "alternate-model.bin"
        write_trace(alternate_src, INPUT_MAGIC, alternate)
        subprocess.run([replay, str(alternate_src), str(alternate_dst)], check=True, capture_output=True)
        queries, results = read_trace(alternate_src), read_trace(alternate_dst)
        assert all(row[:6] == bytes(6) for row in results.records)
        assert compare(queries, results, results)["matched"] == 72
        # A synthetic comparison-flag error must not disappear behind a mask.
        changed = []
        for query, row in zip(alternate, results.records):
            corrupt = bytearray(row)
            if query[0] == 0xDC or (query[0] == 0xDE and 0xD0 <= query[1] <= 0xD7):
                corrupt[9] ^= 1  # status C0 in the saved image
            changed.append(bytes(corrupt))
        reference = Trace(MODEL_MAGIC, results.instruction_address, tuple(changed))
        assert compare(queries, reference, results)["mismatched"] == 24
        tests += 1
        for esc, modrm in ((0xD9, 0xD1), (0xD9, 0xD7), (0xDD, 0xE0),
                           (0xDE, 0xD8), (0xDE, 0xDA), (0xDF, 0xE0)):
            bad = bytearray(alternate[0]);bad[:2] = bytes((esc, modrm))
            write_trace(src, INPUT_MAGIC, [bytes(bad)])
            assert subprocess.run([replay, str(src), str(directory / f"reserved-{esc}-{modrm}.bin")],
                                  capture_output=True).returncode != 0
            tests += 1
        unnormal, labels = make_cases("unnormal-zero")
        assert len(unnormal) == len(labels) == 15360
        unnormal_src, unnormal_dst = directory / "unnormal-input.bin", directory / "unnormal-model.bin"
        write_trace(unnormal_src, INPUT_MAGIC, unnormal)
        subprocess.run([replay, str(unnormal_src), str(unnormal_dst)], check=True, capture_output=True)
        for query, raw in zip(unnormal, read_trace(unnormal_dst).records):
            result, gaps, sw, tags, registers = outcome(raw)
            assert result == gaps == 0
            initial_sw, initial_tags = struct.unpack_from("<HH", query, 4)
            top = (initial_sw >> 11) & 7
            old = tuple((e, s) for s, e in struct.iter_unpack("<QH", query[16:]))
            zero_first = old[0][1] == 0
            exponent, significand = old[int(zero_first)]
            if (query[1] == 0xE1 and zero_first) or (query[1] == 0xE9 and not zero_first):
                exponent ^= 0x8000
            control, = struct.unpack_from("<H", query, 2)
            pc = (24, None, 53, 64)[(control >> 8) & 3]
            rc = (control >> 10) & 3
            unit = 2 ** (64 - pc)
            exact = Fraction(significand, unit) * (-1 if exponent & 0x8000 else 1)
            rounded = (round(exact) if rc == 0 else exact // 1 if rc == 1 else
                       -((-exact) // 1) if rc == 2 else int(exact))
            inexact = rounded != exact
            increment = abs(rounded) > abs(exact)
            assert registers[0] == (exponent, abs(rounded) * unit)
            assert registers[1:] == old[1:]
            assert sw == (initial_sw & ~0x200) | (0x20 if inexact else 0) | (0x200 if increment else 0)
            assert tags == initial_tags & ~(3 << (2 * top))
        tests += 1
    print(f"PASS: {tests} trace-tool scenarios; {len(records)} smoke and {len(rounding)} FRNDINT model-only round trips; no hardware evidence")

if __name__ == "__main__":
    main()
