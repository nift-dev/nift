# CP-E reproduction sources and raw results

Run from the Nift repository root on Linux with an optimized embedding archive.
Probes use glibc `malloc_trim` solely to distinguish freed allocator pages from
live registry storage. No production implementation is modified.

```sh
mkdir -p .build/cp-e
cp docs/evidence/v48-cp-e/*.cpp docs/evidence/v48-cp-e/*.c docs/evidence/v48-cp-e/*.n docs/evidence/v48-cp-e/*.py .build/cp-e/
make libnift_c.a
c++ -std=c++17 -O2 -Iinclude .build/cp-e/probe.cpp libnift_c.a -ldl -pthread -o .build/cp-e/probe
c++ -std=c++17 -O2 -Iinclude .build/cp-e/lifecycle.cpp libnift_c.a -ldl -pthread -o .build/cp-e/lifecycle
cc -shared -fPIC .build/cp-e/fixture.c -o .build/cp-e/fixture.so
python3 .build/cp-e/matrix.py
python3 .build/cp-e/extended.py
.build/cp-e/probe 10000 100 fresh 5
.build/cp-e/probe 1000000 100 drop 1 array
.build/cp-e/probe 1000000 100 drop 1 bytes
.build/cp-e/lifecycle
./nift .build/cp-e/semantics.n
/usr/bin/time -v ./nift .build/cp-e/reuse.n
valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=97 .build/cp-e/probe 1000 100 drop
valgrind --leak-check=full --error-exitcode=97 .build/cp-e/probe 1000 100 callback
valgrind --leak-check=full --error-exitcode=97 .build/cp-e/lifecycle
valgrind --tool=massif --massif-out-file=.build/cp-e/massif.out .build/cp-e/probe 10000 100 drop
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 .build/nift-sanitize .build/cp-e/semantics.n
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 .build/nift-sanitize .build/cp-e/stress.n
```

The sanitizer binary is the existing accepted-runtime build, not created by
this directory. LeakSanitizer requires a host environment permitting its thread
inspection. Fixture output is `77,77,77,77,42`; lifecycle output confirms native
pointer survival across executions, rejection of injected reserved handles,
and survival of result text after owner destruction.

`matrix.json` is the initial 45-row matrix; `extended-matrix.json` is the expanded
140-row matrix. Every row exits zero. `fresh.log` records bounded fresh-Engine
behavior; `input-kinds.log` records array/bytes comparisons. `reuse.log` includes
isolated CLI peak RSS. Valgrind logs certify zero errors and zero blocks at exit;
`massif-peak.txt` is the peak useful-heap stack tree. Empty sanitizer stress log
means no diagnostics; the invocation exited zero. `hosted-closeout.json` records
exact SHA and successful jobs/steps; `followup-actions.json` confirms the sole
workflow triggered at the test-only follow-up is successful. All other hosted
walls are qualified in the parent report to their accepted-runtime SHA.
