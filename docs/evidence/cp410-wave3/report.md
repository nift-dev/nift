# Nift v4.10: bounded traversal and replacement tranche

This tranche retains two small, tested implementation changes while the separate Silverback review investigates structural Nift costs. It does not adopt the rejected callable snapshot wrapper, file-capacity model, object indexes, prepared dispatch experiments, or Jsonic++ investigation.

## Retained changes

Recursive glob components sharing a directory now share one operation-local directory-entry scan. Descendant directories receive independent scans; final absolute lexical ordering, deduplication and per-result canonical presentation remain. On the 120k local fixture, scans fall from 346 to 173 getdents64 calls, instructions from 5,356,232,167 to 4,376,109,795 (18.3%), allocations from 7,440,409 to 5,760,386 (22.6%), and allocated bytes from 1,334,489,192 to 975,032,804 (26.9%). Six isolated paired samples give median CPU 1.84495 → 1.73032 seconds and wall 1.85207 → 1.73699 seconds (6.2%); RSS is essentially unchanged. These are incremental results against the accepted first glob tranche, not official benchmark results or a claim that the external traversal gap is closed.

String replacement builds the output in one pass over the original input rather than repeatedly shifting a growing string. Empty-old/type/error behavior remains. The growing UTF-8 local fixture falls from 10,936,690,669 to 8,179,118,741 instructions (25.2%), with median CPU 0.56269 → 0.36112 seconds (1.56×). Small no-match/UTF-8 cases have near-neutral instructions and allocation counts; small repeated timings remain neutral. Remaining source parsing and dispatch costs are addressed by research models, not this production change.

## Certification

`safety/progress.log` records all 13 groups PASS: focused parity/guards, native embedding/bindings, GCC/Clang warnings, binding warnings, NRS (93), PRS (12), lifetime sanitizers, 58 additional root/capture/async reproducers, sanitized object contracts (86), sanitized sort contracts (31), core memory corpus (57), Deep sanitizer corpus, and parser fuzzing (1,219; 232 builds and 987 controlled errors). `real-build-parity.json` records identical generated files and idempotent incremental builds for default, migration, rewrite and redesign fixtures. Focused final runs include 20 replacement cases and 12 POSIX glob cases. Windows has equivalent platform-valid cases and preserves UTF-8 stdin transport.

The first timing attempt overlapped a guard compiler and was discarded; `acceptance-timing.json` is the completed isolated acceptance run. Earlier exploratory timing files are retained with their limitations, not averaged into acceptance. Binary identities are recorded separately.

## Dropped work

The scope-wide shared capture wrapper reduced ordinary identity-sort instructions about 14% but regressed loops 7.5% and scalar calls 5.4%; no architecture promotion. Releasing closed file buffers reduced 100k-file RSS about 15.9% but regressed CPU 3.7%; no production change. Jsonic++ strict-wide duplicate membership results remain evidence only: the user cancelled all dependency changes and pin-policy work. Canonical header/tests were restored to their exact pre-campaign HEAD; consumer copies and synchronization policy were never modified.

## Hosted checks and frozen evidence

Earlier Windows failures were deterministic fixture transport faults: native argv codepage decoding, invalid star filenames mapped by MSYS, and cygpath converting glob star syntax to U+F02A. Failure logs and explicit fixes are preserved. None was dismissed as flaky. The current production tranche requires hosted certification on its exact published SHA; older green SHAs are historical only. ABI remains 1.3. All 51 frozen expanded-shell evidence files are unchanged; no official benchmarks were rerun.
