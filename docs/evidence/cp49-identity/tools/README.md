Run from the core repository root. These tools use the saved CP49 probe inputs
and binaries in `.build`, as described in the report. They never edit production
source or the official benchmark repository.

Run `profile.py`, then `build_phases.py` and `measure_phases.py` sequentially.
Run `destruction.py` and `observability.py`. Finish all compilation/Valgrind jobs
before `long_cpu.py`; do not launch overlapping copies writing the same files.

The insertion mechanism uses the generated phase header/allocation counter:

```sh
g++ -std=c++17 -O2 -Isrc -Iinclude -Ijsonic/include \
  -I.build/cp49-identity/phases \
  docs/evidence/cp49-identity/tools/insertion_probe.cpp \
  .build/cp49-identity/phases/Alloc.o dist/embed-prefix/lib/libnift_c.a \
  -lffi -ldl -pthread -o .build/cp49-identity/insertion-probe
.build/cp49-identity/insertion-probe
```

For the mechanism sanitizer, compile the probe and generated Alloc.cpp with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, then run with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.
The unchanged static embedding archive is not instrumented by that command;
this is a small mechanism check, not full core recertification.

Fresh dispatch and value counts reuse the exact accepted wave-2 diagnostic
binaries. Value-counter copied production C++ sources were checked byte-for-byte
against current source before those runs. The corresponding instrumentation
builders are in `docs/evidence/cp49-wave2/tools/`.
