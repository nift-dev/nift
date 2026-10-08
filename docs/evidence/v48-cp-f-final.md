# NIFT v4.8 — diagnostic source-map implementation and certification

Starting accepted runtime: `ab687d07d4f987b09ac0277142e2aaf1c0602af6`.
Starting HEAD: `f2f243c92e083bbab46099a07c84a6f6ec856f16`.
Certified runtime implementation: `7c81de3faee816231abe36c464c2d8327da305c7`.

The user accepted the investigation and explicitly approved the general source-map
infrastructure. The historical [investigation](v48-cp-f-investigation.md) and
[before observations](v48-cp-f/README.md) remain available. That baseline had
39 canonical-location mismatches in 45 cases. The committed regression oracle
now passes all 45 cases while preserving frozen messages, codes, disposition,
output, and frame order/labels. Attribution sites now use original source.

## Canonical model and scope

An immutable internal `SourceDocument` owns original bytes, diagnostic path, and
line-start index. Shared `SourceView` instances compose sparse transformed ranges
directly into original byte ranges. Identity and contiguous linear slices avoid
mapping allocation; translated wrappers anchor to their originating construct.
Trimming, dedentation, nested extraction, native translation, AST offsets, legacy
evaluation, templates/includes, imports, callable definitions, callbacks, struct
fields/methods and async workers preserve their source views.

Known expression failures retain the deepest original range. Generated syntax
uses a deliberate construct anchor. Columns count original bytes, including tabs;
CRLF excerpts omit the carriage return. End-of-input anchors survive composition.
Both public errors and typed diagnostics project the same canonical origin.
Callees retain their defining document, with callers represented as frames.

Authority/provenance remains separate from coordinate mapping. Source views bind
to fresh callable/lambda instances; B5 syntax and numeric caches retain no source
documents, paths, Parser pointers, or invocation state. The public diagnostic API,
C ABI 1.3, RuntimeValue representation, diagnostic taxonomy, recoverability,
callback semantics and FFI buffer lifetimes remain unchanged. No package or
benchmark repositories, version numbers, tags or Release workflows were changed.

The stale diagnostic registry snapshot was repaired independently in `42ca5d0`:
62 codes, including the existing IoMetadataFailed, with uniqueness and fingerprint
checks retained. A separate registry documentation repair (`f134c16`) replaces an
obsolete external count needle with the authoritative regression runner reference;
the historical public claim remains NOT_ESTABLISHED.

## Local certification

- Full `make -j2 test`: GCC and Clang PASS, including migration and diagnostic gates.
- Source-map oracle: 45/45 PASS, including exact/anchor columns, paths, lines,
  excerpts, public/typed range agreement and frozen diagnostic contracts.
- Additional source-view, translation, AST span, field and repeated worker tests: PASS.
- Numeric/callback/fallback parity: 470 pairs PASS.
- ASan/UBSan source-map oracle and definition/worker lifetime tests: PASS.
- Sanitized parser fuzz: 1,219 cases PASS (232 builds, 987 controlled errors).
- Sanitized core lifecycle: 57 command/test phases PASS.
- Lifetime sanitizer: full shallow corpus and profile canary PASS after repairing
  missing header depfile inclusion (stale objects had retained the old Parser size).
- Tracking/full-build/recovery/scripting scaling guards: PASS.
- Static test integrity: 283 files, zero findings.
- Guarantee registry and version consistency: PASS.
- Pinned NRS `579793309a30188538e4cc618a61247f636a81cc`: 93/93 PASS.
- Pinned PRS `1da43659da269c96af21aea7234799d2ad56b2df`: 12/12 PASS.

The broad aggregate initially reproduced an existing package-fixture mismatch
on both baseline and candidate: current local curl exports `curl.request`, while
the historical combined fixture calls top-level `request`. Certification uses
an isolated checkout with the seven hosted-pinned package revisions (see
[package pins](v48-cp-f/measurements/package-pins.json)); original package
worktrees remain untouched. Two embedding assertions were updated from
transformed/caller columns to original return/definition directive anchors,
without changing message, path, line, output or failure assertions.

The required C ABI and maintained binding gates passed (C#, Go, Node and Python).
The supplemental historical v4.6 CP14 aggregate is not a CP-F completion gate:
it recursively repeats completed gates, tests the old source-freeze boundary,
and invokes TSan beyond the requested ASan/UBSan certification. Its initial
stale coordinate checks were corrected and focused gates passed; its final
supplemental rerun was stopped deliberately, not recorded as PASS.
Hosted certification is recorded in the final closeout after completion. This implementation report alone does not claim
CP-F closed before those gates are green.

## Coherent implementation checkpoints

| Commit | Change |
| --- | --- |
| `42ca5d0` | Separate diagnostic registry snapshot repair |
| `2088c45` | Internal document/view primitives |
| `2cc18f1` | CP-E Parser-owned FFI lifetime documentation |
| `342c9d6` | Native translation and normalized fragments |
| `c987429` | AST spans and legacy argument slices |
| `c4dccd8` | Prepared loop enclosing offsets |
| `7cec32c` | Template normalization views |
| `3bb8715` | Deepest mapped legacy failure |
| `235c67f` | Callable instance definition views |
| `3e6bf8f` | Prepared execution views |
| `1dbcf2a` | Unified canonical projection and 45-case oracle |
| `acc05d1` | Condition-view reuse and allocation-free linear slices |
| `7c81de3` | Struct field and async worker definition origins |
| `f134c16` | Authoritative external registry reference |
| `1f8a9f2` | Lifetime sanitizer header dependency tracking |
| `97f20ac` | Original return/fragment anchors in embedding assertions |
| `5b7b01d` | Original import failure coordinates and callee definition check |

## Measured cost

[Raw samples and reproducible profiling sources](v48-cp-f/measurements/overhead.json)
compare exact runtime 7c81de3 with a reconstructed ab687d0 using GCC, two warmups
and seven balanced interleaved rounds. Whole-process measurements include startup;
short fixtures are noisy and these ratios are observations, not guarantees.

| Fixture | Paired median candidate/baseline wall time | Baseline/candidate median peak RSS (KiB) |
| --- | ---: | ---: |
| Small native | 1.057 | 7016 / 7040 |
| Large native | 1.075 | 10624 / 12152 |
| Prepared native loop | 1.028 | 6940 / 6960 |
| Cached callbacks | 1.067 | 7476 / 7592 |
| Template heavy | 1.054 | 7412 / 7516 |
| Migrated-style site | 1.052 | 7616 / 7484 |

`SourceView` is 56 bytes and each sparse span is 40 bytes on this 64-bit build.
The 137,793-byte native fixture generates 30,003 spans, 1,200,120 logical mapping
bytes and 80,016 line-index bytes. Median peak RSS rises 1,528 KiB. Logical span
bytes exclude allocator capacity; observed RSS includes allocation costs. Original
text and indexes are shared by live instances, without global cache retention.
Actual untransformed template identity views allocate no sparse mapping array.
The profiler's HTML sample is native structural classification of HTML-like text,
not the template identity pipeline.

Isolated translation plus structural classification costs 14.33→15.90 µs for
191 bytes, 7.31→9.31 ms for 138 KB, and 6.03→8.75 µs for the loop fixture.
These isolated costs differ from whole execution and are reported explicitly.
Prepared conditions reuse their views outside the hot loop; contiguous slices
avoid allocation. There is no new optimization campaign.

## Completion boundary

After the required hosted walls pass, CP-F closes the v4.8 feature queue.
Recommended next phase: feature freeze and stabilization. No new performance,
FFI release, representation, callback-storage or migration features are authorized
by this closeout. Release artifacts remain NOT RUN.
