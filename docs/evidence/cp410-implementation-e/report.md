# CP410-E — bounded canonical large-string execution

Decision: retain after the preceding tranche's exact-head hosted certification
and this tranche's final certification. This report records local evidence;
hosted acceptance must be supplied by an exact-head receipt.

The candidate preserves the ordinary 8 KiB preparation decision. A separate
literal-payload parser admits at most 128 KiB, 8 KiB of structural bytes, 64
tokens and 16 levels of nesting, with the existing parser depth guards retained.
Token and nesting rejection precedes AST construction. The adversarial guard
rejects a 120 KiB literal followed by 1,000 chained methods before building it.

Only a certified pure string grammar enters the canonical fast path. Receiver
and argument factories, unsupported types and malformed argument lists retain
canonical behavior. A shared canonical assignment body preserves validation,
mutation and source handling. The syntax-only cache holds at most 16 entries
and a 4 MiB owned-capacity allowance; it retains no values, bindings or source
views. Eighty distinct 120 KiB expressions peak at 3,891,684 accounted bytes.
Canonical and prepared replacement use the same existing linear builder.

Six balanced, CPU-pinned whole-process measurements on the same UTF-8 input
and 1,000 repetitions give:

| Measurement | Accepted CP410-C | Candidate | Native control |
|---|---:|---:|---:|
| Median CPU seconds | 0.3458675 | 0.1355505 | 0.0699765 |
| Median wall seconds | 0.347419856 | 0.137159942 | 0.070580918 |
| Median peak RSS KiB | 7504 | 7742 | 4012 |
| Instructions | 8,183,733,401 | 2,888,914,775 | See raw control evidence |
| Allocations | 59,279 | 20,332 | 3,004 |
| Cumulative allocated bytes | 553,512,174 | 235,586,141 | 57,453,498 |

The retained shape yields 2.55× CPU and 2.83× instruction improvement, with
238 KiB additional peak RSS. All three Memcheck runs have zero errors and
zero heap bytes at exit. Native control matches output bytes but omits language
validation, general binding and source contracts; it is a descriptive floor.

Scaling measures whole assignment source sizes, including the method wrapper,
at 128 iterations. The 8 KiB boundary is effectively neutral; 16–128 KiB cases
each reduce instructions by about 2.10–2.11×. The larger headline fixture has
different literal content and repetition count. Do not conflate the two.

The earlier RHS-only experiment left assignment dispatch scans intact and
achieved only about 1.6× CPU. It is superseded. The earlier semantically invalid
prototype's 4.6× CPU result is not a production result. Exact speculative and
canonical evaluation behavior explains part of the difference.

Current residual instructions include string comparisons, copies and append,
canonical body/source processing, and bytewise uppercasing. The final profile
and annotation replace stale pre-assignment percentages. Eight ordinary control
workloads remain within approximately 0.8% instruction overhead; raw figures
are retained, including map/set and callback controls.

Permanent tests include 37 independent exact-oracle cases covering large UTF-8
expressions, replacement shapes, malformed calls, invalid types, mixed prepared
bodies, factory counts and diagnostic origins. A separate C++ guard covers cache
bounds and preconstruction rejection. No global preparation limit is raised.

The private and complete certification CLI binaries are byte-identical. Native,
binding, GCC/Clang warning, lifetime and deep sanitizer suites pass, as do NRS
93 modules, PRS 12 modules and all four default/migration/rewrite/redesign build
scaffolds. Normal and lifetime-sanitized core-memory checks each pass 57 phases
over four rounds. Deep fuzz passes 1,219 cases: 232 builds and 987 controlled
errors. The cache/preconstruction adversarial guard passes ASan, UBSan and LSan.
Initial extra sanitizer invocations under the tracing sandbox failed because
LeakSanitizer cannot inspect threads there; those logs are retained, followed
by runs outside that restriction. No runtime fix or unchanged workflow rerun
was used to conceal an error.

Jsonic++, dependency policy, frozen external benchmark evidence, publications
and ABI 1.3 remain outside this change. Reproduction scripts and binary/source
identities accompany the measurements.
