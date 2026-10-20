#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parser/provenance round trips, NOT hardware-accuracy tests."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from trace_format import INPUT_MAGIC, MODEL_MAGIC, HEADER, read_trace, write_trace
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
            assert subprocess.run([replay, str(src), str(dst)], capture_output=True).returncode != 0
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
        assert subprocess.run([replay, str(src), str(dst)], capture_output=True).returncode != 0
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
        assert subprocess.run([replay, str(src), str(dst)], capture_output=True).returncode != 0
        tests += 1
        bad = bytearray(records[0]);bad[0:2]=b"\xdf\xe0" # modern FNSTSW AX
        write_trace(src, INPUT_MAGIC, [bytes(bad)])
        assert subprocess.run([replay, str(src), str(dst)], capture_output=True).returncode != 0
        tests += 1
        # Research gaps are retained as blocked, not hidden in a passing tally.
        research, _ = make_cases("research")
        write_trace(src, INPUT_MAGIC, research)
        subprocess.run([replay, str(src), str(dst)], check=True, capture_output=True)
        results=read_trace(dst)
        assert any(int.from_bytes(r[:2],"little")==2 for r in results.records)
        tests += 1
    print(f"PASS: {tests} trace-tool scenarios; {len(records)} model-only round trips; no hardware evidence")

if __name__ == "__main__":
    main()
