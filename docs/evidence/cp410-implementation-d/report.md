# CP410-D — prepared FileValue operations and buffer ownership

Conditional KEEP. The 2× tiny-save CPU target was not achieved: the final paired result is 1.22×. The ownership and dispatch changes nevertheless remove 41.1% of whole-probe instructions, 48.2% of allocated bytes in the large write/save case, and half the retained buffer capacity in the clean read/write-open case. The custom concat reader is DROP.

Baseline: `e290b85cdb655b3490e051262451a346baf00309`, binary `bf9771454572e1d866a38ffa7b864d8c7ce33e2455ad0b0af55c22ae9b04c30d`.
Final native binary: `f3f999b247fe5375902306f983311402225ca279947dd1b16a6073830db62dd0`. Main, private prototype, and complete certification stage are byte-identical. `identities.json` records the nine source/build/oracle hashes. Source changes are reproducible from `source.patch`; other prototype scripts/logs are historical experiments, not the final build recipe.

## Retained implementation

- A shared canonical FileValue method executor retains lazy argument evaluation, precondition/error order and original recursive token evaluation. The prepared native writer retains its different existing argument/precondition order.
- File factory operations reuse the canonical filesystem path/policy backend and allocate fresh IDs. Source-aware recipes carry syntax and offsets, not values, bindings, cwd or source-view owners. The recursive canonical cache is limited to 32 entries and a conservative 4 MiB charge; source length is at most 4 KiB. Pure operands retain the original 96-depth guard through the conservative depth-79 eligibility boundary.
- A clean saved state aliases the working state. A real byte-changing mutation first snapshots the old working bytes. No-op mutations avoid that copy. Write-mode open still retains the old on-disk bytes for revert. Dirty detection, failed saves, external edits, aliases and reopen behavior remain unchanged.
- After the existing renderability checks, both writers move a temporary String into their local byte owner. Evaluating a binding still copies into that temporary: source String slots and their aliases remain intact.
- Close retains the original opaque ID/registry shell and buffer capacities. No reclamation or shrink-capacity policy was added. Atomic replacement and copy I/O are unchanged.

## Save measurements

Fresh 1,000-file, 128-byte output fixtures; six alternating native/accepted/final triples; every output byte checked. CPU/RSS timing occurred after campaign compilers stopped.

| Metric | Accepted | Final | Native atomic control |
|---|---:|---:|---:|
| Instructions | 204,530,528 | 120,457,677 | separate descriptive floor |
| Median CPU seconds | 0.036480 | 0.0300145 | 0.011501 |
| Median wall seconds | 0.038779 | 0.031898 | 0.012734 |
| Median peak RSS KiB | 8,202 | 8,232 | 4,220 |

The final process is still 2.61× the native atomic control in CPU. That control omits Nift source/value/path-policy work, FileValue IDs, retained shells and state checks; it is a lower bound, not a proof that the remaining gap is irreducible. Further source/dispatch/lifecycle costs remain. The 2× save target is explicitly missed.

Six Memcheck runs (1/128 bytes × 1,000 files and 1 MiB × 32 files) have zero errors and zero exit heap. For the 32 × 1 MiB write/save case, allocations fall 3,298 → 2,898 and allocated bytes 139,180,619 → 72,064,543. Allocation totals are not mislabeled as copied bytes.

## Direct copy and retention evidence

`copycounts/results.json` instruments completed saved/working String assignment copies in independent accepted/final stages with unchanged class layouts. These counters exclude stream reading, RuntimeValue/argument copies and mutation replacement copies. Each of 32 files is 1 MiB; output bytes are checked.

| Scenario | Accepted assignment-copy bytes | Final assignment-copy bytes |
|---|---:|---:|
| Clean rw open/close | 67,108,864 | 0 |
| Fresh write/save/close | 33,554,432 | 0 |
| rw mutation/revert/close | 100,663,296 | 67,108,864 |

The last case still performs the required lazy saved snapshot and revert copy. This is copy-on-write, not removal of revert semantics.

For 32 existing 1 MiB files opened rw and closed cleanly, retained working/saved capacity changes from 32 MiB + 32 MiB to 32 MiB + 480 bytes of empty String capacity. The independent capacity unit measured peak RSS 73,372 → 42,436 KiB. Registry cardinality remains 32. This result is shape-specific: a genuinely mutated file can legitimately retain both capacities.

## Concat experiments

The original owned stream sink regressed large-input instructions. A buffered sink preserved 250 controlled binary/EOF/partial-error transfers and input refill counts, but still increased large-input instructions about 48%; pilot CPU gains were only about 7–10%. Both readers and their open recipes are dropped. No custom reader, I/O fusion, disk spool or input streaming claim ships.

The retained two-line write ownership change uses the original reader. Against the recipe-only save candidate, the 32 × 1 MiB concat probe changes instructions 55,527,624 → 51,808,290 and allocated bytes 201,047,367 → 167,492,915. Final six-pair CPU is 80.073 → 78.603 ms; tiny concat is effectively neutral (13.008 → 13.262 ms). This is a small, general, trivial copy removal, not a concat architecture win. Full output hashes match. Complete FileValue working state remains necessary for dirty/revert/read/alias behavior before atomic save.

## Deterministic sanitizer failure and fix

The expanded oracle reproduced a stack overflow in the already accepted lifetime binary: the compatibility evaluator had a 218,128-byte frame. Nested named calls at 40 levels overflowed before reaching the existing parser guard. A first split fixed named calls but failed method chains; correcting an exploratory unsupported `trim()` builtin case to genuine `html_escape()` then exposed the remaining native-dispatch overflow at 80 levels. All raw failures are retained.

Independent dispatch/family frames are enabled only in ASan builds. The ordinary native dispatch structure remains intact. The final lifetime compatibility frame is 39,760 bytes, with smaller independently measured family frames. No stack limit was raised, no recursion guard was reduced, and no test was weakened. The permanent oracle contains 102 cases, including valid native `html_escape`, `type`, `min`, method receivers and method arguments through 110 levels. Results and depth-error spans match the independent accepted normal oracle under full lifetime sanitizers.

## Certification and controls

The complete final stage passed focused contracts/guards, native and binding suites, GCC/Clang first-party warnings, binding warnings, ASan/UBSan/LSan lifetime and deep profiles, all 102 file/depth oracles, the 97 filesystem/pure-operand oracles, and prior string/glob oracles. Parser fuzzing passed 1,219 cases (232 builds, 987 controlled errors). Normal and lifetime core-memory runs each passed 57 phases across four rounds. NRS passed 93 modules, PRS 12. The refreshed embedding library reports ABI 1.3 and matches its staged prefix byte-for-byte.

Eight instruction controls: loops +0.061%, scalar calls +0.184%, no-arg calls +0.028%, closure calls +1.272%, callbacks +1.107%, BFS +0.073%, JSON traversal +0.079%, map/set −0.047%. The small closure/callback overhead is disclosed. No JSON-transform control was silently substituted for an absent matching probe.

Default/migration/rewrite/redesign init/status/full-build/incremental workflows have exact artifact parity; the actual website clone matches all 173 public artifact hashes. No material real-build speedup is claimed.

All 81 frozen official/expanded evidence hashes remain unchanged. Canonical Jsonic++ and Minify++ remain at their accepted released state. No dependency pin/synchronization policy, RuntimeValue index, general scope replacement, VM/JIT, release, Linode or Labs change is included.

Hosted certification is performed on the introducing commit after publication: all ten prescribed non-release walls must be green at that exact head. The exact-head receipt and run URLs are reported with the completed campaign; no local result is presented as a hosted pass.
