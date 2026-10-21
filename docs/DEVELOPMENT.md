# Development and review

Use a feature branch and a pull request for changes. Group related accuracy
work in a larger pull request linked to the existing subsystem issues, with
primary-source references, reproducers and acceptance criteria. Create a new
issue for distinct missing work rather than every smaller correction. Close
an issue only when its full implementation and software-validation criteria
are met. Physical hardware access is not required; use original documentation,
first-hand research and independent software references. Published captures
are optional corroboration.

CI runs all eight suites with GCC on Linux, Clang with AddressSanitizer
and UndefinedBehaviorSanitizer on Linux, and MSVC on Windows. Compiler warnings
are errors. The GCC job checks the supported Python minimum; the other jobs use
a current interpreter. CI verifies test discovery so a missing interpreter
cannot silently remove the independent numerical suites. Test reports are
retained as artifacts.
The replay tool and numerical test driver retain ISO C stdio on MSVC through
the target-local `_CRT_SECURE_NO_WARNINGS` setting; other warnings stay enabled.

A separate job builds the library, replay tool and demo with tests disabled.
It also rebuilds the DOS collector and compares it with the supplied binary.
Assembly and binary reproducibility do not validate execution on an 8087.
The collector artifact is labeled accordingly. There is no automatic package
publication or deployment.

Require every CI job to pass on the final PR head before merging. If code changes
after a successful run, wait for the new checks. Read the diff and inspect the
test results; a green workflow is not evidence of hardware equivalence.

For a local GCC build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-Werror
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with a Visual Studio generator, use `-DCMAKE_C_FLAGS=/WX`, add
`--config Release` to the build, and add `-C Release` to CTest. MinGW GCC also
works with Ninja. If copied source timestamps are ahead of the system clock,
correct the timestamps or configure with `-DCMAKE_SUPPRESS_REGENERATION=ON`
and explicitly rerun CMake after build configuration changes.

The root `SHA256SUMS` records the imported package. It is historical
provenance, not a manifest for later commits. Git identifies current source
revisions; hardware campaigns need their own hashes and machine metadata as
described in [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md).
