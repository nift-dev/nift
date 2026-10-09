# CP410-F — actual RuntimeValue object workloads

Decision: defer persistent object indexing. No object representation or Jsonic++
production change is proposed. The 2k/8k repeated-read regime from the synthetic
index model does not appear in the measured workloads.

A private CLI observer records object width, lookup operations, registered
member assignments, source copies, range traversal starts and insertions.
Object identity follows moves; destruction closes the lifetime record. The
independent two-object oracle proves the expected read/write/copy/iteration
counts. Each workload matches the accepted CLI's status and output, or its
generated public artifacts. Instrumented timing and allocation costs are not
performance evidence.

| Workload | Largest RuntimeValue object | Lookup operations | Registered member assignments | Source copies | Object range starts |
|---|---:|---:|---:|---:|---:|
| Official-equivalent JSON transform, 1k records | 5 | 12,000 | 8,000 | 2,000 | 0 |
| JSON traverse, 8k records | 5 | 48,000 | 40,000 | 24,000 | 0 |
| Default/migration/rewrite/redesign lifecycle, each | 0 | 2 | 0 | 0 | 0 |
| Actual website rebuild, 173 public artifacts | 0 | 106 | 0 | 0 | 0 |

The five-member objects are loop metadata. Data records have only a few keys;
transformation adds one member. Array iteration is not counted as object
iteration. The observed width/read distributions contain no object with both
at least 2,000 members and at least 2,000 lookup operations.

The four scaffold lifecycles include init, status, full build and incremental
build in independent accepted/instrumented roots. A separate copy of the
actual website builds the same 173 public artifacts byte for byte. Its initial
fixture omitted static assets, causing the accepted CLI to fail and leave its
normal unfinished-build marker. Recreating independent complete copies fixed
the fixture; the original website was never modified or published.

On-disk configuration and tracking JSON shapes are reported separately. They
reach 16 members in these projects and are handled through the existing JSON
document representation. A disk JSON width is not a RuntimeValue access count.

Counter coverage is explicit. Lookup counts include membership preflight for
mutable access; they are not all pure reads. Assignment counts cover members
registered through the RuntimeValue accessors, and insertions are separate.
Public-vector mutation and direct member-field writes can bypass those hooks.
Range counts are traversal starts at the recorded sites, including early
returns, not inferred full member visits. Width is the maximum observed during
a value instance's lifetime. These limitations prevent claiming exhaustive
object mutation counts, but do not establish a wide hot lookup workload where
none was observed. Raw per-object distributions and instrumentation sites are
retained for inspection.

The earlier index model's roughly 114× hot lookup result and roughly 5.3× cold
copy regression remain diagnostic evidence. These workloads are narrow and
copy-heavy. An eagerly built index would add cost before a handful of reads
could amortize it, while the public vector still prevents reliable invalidation.
Continue using linear member lookup. Reopen indexing only for an actual wide,
repeated-read workload and a separately reviewed mutation/ownership design.
