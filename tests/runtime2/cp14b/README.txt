CP14B focused gates

Run heavy builds and gates serially with the established shared heavy lock.
Use an accepted-foundation binary as CP14B_BASE; no network or provisioning is
needed. Both ordinary and switch dispatch are required.

make -j2 nift .build/cp13b/nift-switch
make test-runtime2-cp14b test-runtime2-cp14b-sort CP14B_BASE=/absolute/base-nift
python3 tests/runtime2/cp14b/context_matrix.py --nift /absolute/nift --base /absolute/base-nift --output contexts.json
python3 tests/runtime2/cp14b/inventory.py --nift /absolute/nift --base /absolute/base-nift --probes tests/runtime2/cp14b/fixtures/legacy-probes.json --output inventory.json
python3 tests/runtime2/cp14b/build_drain.py --out /absolute/drain
python3 tests/runtime2/cp14b/mutations.py --nift /absolute/nift --out /absolute/mutants
python3 tests/runtime2/cp14b/build_sanitizer.py --out /absolute/san

For each sanitizer binary cp14b-san and cp14b-san-switch:
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tests/runtime2/cp14b/functional.py --nift /absolute/san-binary --legacy /absolute/base-nift --output san-functional.json
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tests/runtime2/cp14b/sort_replay.py --sanitizer --nift /absolute/san-binary --oracle tests/runtime2/cp14b/fixtures/d4-traces.json.gz --output san-sort.json

The source-hashed harness links the real runtime and reports CLI-compatible
uncaught diagnostics, but deliberately has no legacy fallback. No sanitizer
report, signal or timeout counts as a semantic mutant detection. The default
D4 replay uses bounded subprocess limits; --sanitizer permits ASan's shadow
address space. Leak detection requires an environment that permits LSAN to run
outside ptrace. The allocation harness forbids new during real caught cleanup,
then injects one failure at each allocation in bounded VM workloads and checks
fatal stop and owned-heap release. Thread-lifetime atom charges are separate
existing CP23 context debt, not charged to leaked collection state.

The frozen native oracle must not be regenerated as an incidental test run.
Its closed pre-controller campaign used 507 child runs and 42415 callback
requests (caps 512 and 65536). Production replays are not new oracle children.
SHA256.json records both compressed and original frozen evidence hashes.

The original inventory includes an unsafe structural comparator. inventory.py
checks its candidate pre-execution refusal and does not run it under legacy.
D-040 independent-copy results are explicitly asserted, not called exact
legacy parity. Retained receiver shapes and unresolved safety remain CP25 debt.

resources.py records preliminary local map/filter/sort_by results at 100k and
1M with raw samples, exact-child RSS and VM heap statistics. This is not the
future dedicated Linode campaign, and it does not authorize infrastructure.
The delivery records unchanged existing native guard results and million-catch
Callgrind counts separately from collection wall time.

The complete approved design mutation strategy is also reproducible:
make test-runtime2-cp14b-design-mutants CP14B_BASE=/absolute/base-nift
This builds twelve fresh named variants, including ignored results/direction,
mutation of a copy in four binding forms, a test-only small frame budget,
missing reporting metadata, factory hoisting and early splice replacement.
Source-copy injections never edit production files. Every variant has a fresh
marker/hash, a passing exact normal oracle (or the explicit small-budget stop),
an unaffected control, and negative classifier controls. Missing/malformed or
duplicate stats, fallback, stale identity, signals, unrelated errors, timeout
and wrong marker are harness failures, never semantic detections. The normal
and mutant design drivers link the actual final runtime objects.
