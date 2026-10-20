# i8087: Intel 8087 emulator core in C11

An instruction-level 8087 implementation with an integer-only
80-bit arithmetic backend. The library does not use host floating-point
arithmetic, `long double`, `libm`, inline assembly, or compiler-specific 128-bit
integers.

**This is not yet a complete, bit-exact or cycle-exact replica.** This revision
fixes original-8087 instruction and exception behavior and adds independent
boundary tests and hardware-capture tools. No original-hardware captures have
been used to validate it. See [accuracy](docs/ACCURACY.md) and
[changes](docs/CHANGELOG.md).

Known guarded cases return `I8087_UNVERIFIED` without committing architectural
state or an operand write. Approximate fallbacks require explicit opt-in. The
guard is not exhaustive and is not a hardware-certification mode.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/i8087_demo
```

The library needs a C11 compiler and the standard C library. Three additional
test suites use Python and its standard library; CMake skips those suites
when Python is unavailable. No third-party numerical library is required.

The demo executes encoded instructions for `(2 + 3) * 4`:

```text
(2 + 3) * 4 = 20
Control=03bf Status=0000 Tags=ffff
```

A direct build is also possible:

```sh
cc -std=c11 -O2 -Iinclude -Isrc src/i8087.c src/soft80.c src/transcend.c \
  examples/demo.c -o i8087_demo
```

On multi-configuration CMake generators, add `--config Release` to the build,
`-C Release` to CTest, and use `build/Release/`. CI tests GCC and Clang on Linux
and MSVC on Windows. MinGW GCC also passes the suites locally.

## Project layout

| Location | Purpose |
| --- | --- |
| `include/i8087.h` | Public per-instance API and raw 80-bit value type |
| `src/i8087.c` | Instruction decoding, state, transfers, exception handling |
| `src/soft80.c` | Integer-software arithmetic and format conversion |
| `src/transcend.c` | Guarded, non-bit-exact transcendental fallbacks |
| `tests/` | Instruction regressions, rational oracles, raw-state safety tests |
| `tools/` | Input-vector generation, C replay, trace comparison |
| `tools/hardware/` | DOS/8086 capture probe; assembled, not hardware-tested |
| `docs/` | API, accuracy matrix, validation, sources, and changelog |

The core exposes eight physical registers, TOP and tags, control/status words,
exception pointers, 8087 ESC decoding, and operand callbacks. The surrounding
8086/8088 emulator must supply effective 20-bit physical addresses, CPU
instruction execution, WAIT synchronization, and any bus/cycle timing model.
The core does not model an entire computer.

Read [the API contract](docs/API.md), [test results](docs/TESTING.md), and
[hardware-validation workflow](docs/HARDWARE_VALIDATION.md) before integration.
See [development](docs/DEVELOPMENT.md) for CI and pull request requirements.
The [roadmap](docs/ROADMAP.md) links the open accuracy issues and
[hardware evidence](docs/research/HARDWARE_EVIDENCE.md) records the initial
8088/8087 target and the search for original captures.
Original documentation and die-analysis references are in
[SOURCES.md](docs/SOURCES.md). Source code in this package is MIT-licensed.
