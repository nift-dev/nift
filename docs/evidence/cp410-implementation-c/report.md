# CP410-C — canonical filesystem recipes and shared operation backend

KEEP move/copy recipes and the shared stat/move/copy backend. This follows the ten-green traversal head `e3ef89d6e8d364088eb7559116b72c6cfe37cf98`. It changes canonical fallback execution, leaving the existing speculative prepared-AST phase in place. It does not release packages, alter dependencies, or modify either frozen benchmark campaign.

## Result

Six balanced repetitions on fresh 10,000-file success fixtures, pinned to one CPU, with no concurrent campaign compilation/profiling during timing:

| Operation | Accepted CPU | Candidate CPU | Native control CPU | CPU speedup | Instructions, accepted → candidate (1,000 files) | Allocations, accepted → candidate |
|---|---:|---:|---:|---:|---:|---:|
| stat | 72.6325 ms | 67.9480 ms | 32.7040 ms | 1.07× | 43,349,876 → 36,683,772 | 58,288 → 55,301 |
| move | 242.4665 ms | 115.1190 ms | 37.4350 ms | 2.11× | 228,496,206 → 73,429,154 | 167,305 → 122,339 |
| copy | 308.5805 ms | 180.7430 ms | 99.7380 ms | 1.71× | 230,069,654 → 74,007,026 | 168,307 → 122,342 |

Move/copy instructions fall about 68%; allocations fall 27%, cumulative allocated bytes fall 7–8%. Median RSS is essentially unchanged: move 10,656 → 10,736 KiB; copy 10,578 → 10,548 KiB; stat 10,620 → 10,720 KiB. All six Memcheck runs have zero errors and zero bytes remaining at exit. Every timing/profile run checks printed results and actual destination bytes.

Copy falls below the initial 2× whole-process CPU target: its native control already spends about 100 ms in the same file-copy success fixture. Removing that control CPU from both measurements gives a descriptive remaining-cost ratio of 2.58×, not a separately measured userspace time. The native control omits general language, validation, diagnostics and source semantics; it is a fixture-specific lower bound. Copy I/O is unchanged. Stat's modest improvement is retained through the same shared backend, without a separate stat architecture.

## Contract and ownership

A body-local source-keyed recipe owns decoded operand syntax and relative source offsets, with no dynamic values, bindings, or source views. The actual invocation derives its operand views from the current call view. Wrong arity, spread, malformed input and one-argument value-copy behavior retain canonical fallback.

The recipe executes inside the original recursive evaluator and file-operation checkpoint/failure-origin handling. Canonical operands are lazy. Each source operand is evaluated, type-checked, resolved, project/root-validated and globbed before the next operand; the destination is evaluated only afterward. Empty-source and validation failures stop at the same boundary. Cwd is observed where the existing resolver observes it, including operand factories that change directories. No global cwd or filesystem cache is added. Both ordinary fallback and recipes call one backend; destination-directory state is local to that invocation.

The conservative pure string plan handles string bindings/literals, string `+`, nonempty literal-delimiter split and first/last. Runtime eligibility is checked before execution. Logical locations are resolved using operation-local owners, and their actual descriptors are synchronized when read. Non-string, opaque-tag, missing, effectful and unsupported inputs use the original evaluator. No values or interior pointers survive an operand invocation. The full split array is still constructed.

Named/module dispatch precedence, factory counts, source/destination ordering, diagnostic positions, recoverable failures and file effects are fixed by permanent independent oracles. Existing speculative evaluation remains observable and is deliberately retained. The cache is local to one prepared parse context; it stores source syntax only. No Parser instance layout or public ABI changes are required.

## Certification and evidence

Permanent tests retain 37 exact filesystem count/order/source/error/file-effect cases and 60 pure-string/fallback cases. Dedicated test-only counters prove one recipe preparation for repeated calls, physical canonical operand counts, pure-plan use, effectful fallback and spread fallback. Counter hooks compile out of the normal build.

The staged full build is byte-identical to the candidate. Native suite, language bindings, GCC/Clang first-party warnings, binding warnings, ASan/UBSan/LSan lifetime corpus, sanitized focused filesystem/glob tests and deep sanitizer build pass. Normal and lifetime-sanitized lifecycle runs each pass 57 phases over four rounds. NRS passes 93 modules; both optional embedding consumers additionally pass against the candidate staged prefix. PRS passes 12 modules. Four default/migration/rewrite/redesign scaffolds have exact init/status/full/incremental output hashes. ABI remains 1.3. The deep-sanitizer generated corpus passes 1,219 cases: 232 builds and 987 controlled errors.

One initial aggregate run was stopped by the sandbox's ptrace restriction on pagination's syscall trace. The independently reproducible `strace true` denial and failed aggregate log are retained; the exact pagination test and complete aggregate pass with tracing permitted. This is an execution-permission failure, not a discarded flaky test. Expected negative-test diagnostics in version-consistency logs are part of their successful tests.

Eight ordinary control instruction counts remain within about 0.13%: loops, scalar/no-argument/closure/callback calls, BFS, JSON traversal and map/set. No JSON-transform control is claimed in that eight-case set. The 81 official/expanded frozen files hash unchanged. Canonical Jsonic++ and Minify++ remain clean at their accepted released heads.

## Rejected and residual work

The recipe-only prototype's roughly 9% gain is superseded. A prototype that rejected logical aliases also missed the real loop workload; it is superseded by the owner-checked current-value path. Their instruction measurements are retained separately.

Current move remains 11.75× and copy 11.02× the native instruction controls; stat remains 7.57×. Residual costs are spread across allocations, RuntimeValue ownership/construction, speculative argument parsing, path component creation/joining and validation. The retained annotation records this shifted profile. This tranche does not pretend that a 3.1× instruction win closes the filesystem gap. Save/concat and the separate large-string canonical plan remain subsequent work, with independent certification and hosted walls.

`manifest.json` hashes the retained measurements, raw logs, compressed profiles, oracles, source identities and reproduction scripts. `identities.json` pins the accepted and candidate binaries. The hosted receipt is added after all ten normal walls finish for the committed source head. The initial cross-platform root-normalization failure and narrow harness correction are preserved in `hosted-portability-fix.md`.

For reproduction, start with an isolated full build of the accepted head, keeping the normal `make` compile/link log. Copy the retained pipeline scripts into `.build/cp410-implementation/filesystem/`, provide that build log as `.build/cp410-implementation/snapshot-build.log`, and run prepare → pure-plan → shared-backend → instrument → build. Those scratch-generation scripts expect the accepted pre-tranche source, not already-patched source. `seed-build.log` and `build.json` retain the actual compile/link recipes. The three retained fixture sources plus the fresh-tree generator in profile/timing/resource scripts specify every success-fixture input; the native source is retained. Control sources are retained in `control-sources.json`. Full certification used an independently archived accepted checkout with the candidate overlay, normal Makefile and permanent tests.
