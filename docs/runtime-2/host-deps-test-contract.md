# host-deps: test-contract repair of the 24 read_limit.py failures (D-077 #3)

This is a tests-only change; no production code changed. It was prepared on `prep/cert-r2` (base C137 `040a6726`)
for independent review, as D-077 #3 requires ("Require independent review of the test changes").

## D-078 update (owner 2026-11-12, attachment 2ea1c509): status of the suite

**`test-runtime2-host-deps` is FAILED**, with six outstanding cases (D-078 #4: no blanket deferral). read_limit.py
prints them as `OUTSTANDING` lines, each with the reason the VM emits and the operation it actually refuses.

| Case | Emitted reason | Operation and classification |
|---|---|---|
| ifstream(/dev/zero).read_all_bytes | host-handle-flow | stream byte read; **finding F-HD1** |
| ifstream(/dev/zero).read_bytes(huge) | host-handle-flow | stream byte read; F-HD1 |
| read_bytes(limit/2) x 16 from /dev/zero | host-handle-flow | stream byte read in a loop; F-HD1 |
| ifstream(/dev/zero) >> token | statement:Command | stream extraction; **finding F-HD2** |
| ifstream(/dev/zero).read_val | host-handle-flow | stream value read, not a byte read; refused by the same generic host-handle-flow fallback as the F-HD1 byte reads; classification not established |
| read_line: line of exactly the limit | type:length | `.length()` on a string-or-null result. The D-078 #3 union deferral is pinned to its corpus sightings and does not cover this case; D-078 #4 keeps it open |

**The two misclassifications (D-078 #4).** Both are corrected in the records only, which the owner allowed: in the
`OUTSTANDING` table here and in read_limit.py, and in the `host-handle-flow` and `statement:Command` rows of
classification.tsv. **Neither reason ID can be corrected without a production diagnostic change, so both stay as
findings.** Neither behaviour is approved, and all six cases remain FAILED.
- **F-HD1.** The VM refuses `s.read_bytes(n)` and `s.read_all_bytes()` on a plain `ifstream` under `host-handle-flow`,
  even though no handle flows anywhere. Probed: `s := ifstream("d.txt")` / `x := s.read_bytes(4)` gives
  `c.f:2:6 … (host-handle-flow)`. The ID names the compiler's generic fallback, not the operation. The correction
  needs an operation-specific reason ID: a production change, not made here.
- **F-HD2.** The VM refuses a `>>` statement as `statement:Command` (a shell command line): `s >> t`, and per the review
  also `x >> 1` on a number. Legacy performs stream extraction for `s >> t`. The correction needs a parser or
  diagnostic change: a production change, not made here.

The count does not match the owner's wording. D-078 #4 speaks of "two misclassifications" and "the other four".
The evidence ties the two misclassifications to four of the six cases (F-HD1: 3, F-HD2: 1), leaving two that are
classified separately. All six stay FAILED either way.

### open_bytes() and read(): named deferrals for the 10 cases (D-078 #2)

These forms were probed on the pinned C137 binary and are recorded in `post_cert_deferrals.tsv`.

**`read()`, the free function that reads one stdin line.**
- **Refused** in every VM-default form probed:
  - as a statement, an assignment or a call argument;
  - in a function body or an uncalled function;
  - in an unreachable `if (false)` branch;
  - in an interactive session (`NIFT_VM session refused: callable:read`).
- **Supported on the VM:** the stream methods on `ifstream`, including `ifstream("/dev/stdin")`. Probed: `read_line()`
  and `read(3)` return the stdin line.
- `read()` itself runs on the legacy channel.

**`open_bytes()`.**
- **Refused** in every VM-default form probed: assignment, argument, function body, uncalled function and unreachable
  branch.
- **Supported on the VM:** `open()`, `file().open()`, `ifstream` `read_all()`, `read_line()` and `read(n)`, `cat()` and
  `sha256_file()`.
- `open_bytes()` itself runs on the legacy channel.

**History.** Both were approved for implementation (owner 2026-11-10 13:26 implement list; D-067 #7, "previously
approved bytes … small-capability items"). They were status `implement` until D-078 #2 scheduled VM support after
the first certification. Both histories are kept in classification.tsv.

### B01 safety coverage that keeps executing through supported VM paths (D-078 #2 condition)

| Guarantee | Tests that still prove it on the VM default |
|---|---|
| **Read limits** (exact limit read whole; one byte over is a fatal, uncatchable limit error) | read_limit_controls.py A (`open`, `file().open`, `ifstream` `read_all`, `read_line`, at 1000 bytes, below one 64 KiB chunk); read_limit.py: `open` and `file().open` exact and over, `ifstream read_all` and `read_line` exact and over, `open`/`cat`/`file().open(r,w)` on /dev/zero and /dev/full, FIFO exact and over, `NIFT_READ_LIMIT` parsing (1K, 2MiB, 1g, 4096, 3m, and the rejected values), the page build that hits the limit; read_limit_contract_controls.py (`open`, `read_all`: limit error, and detection of a removed limit); the C++ unit `read-limit-unit` (src/ReadLimit.h) |
| **Bounded allocations** (RLIMIT_AS = baseline + 1.5 × limit; peak RSS ≤ baseline + limit + slack; never `std::bad_alloc`) | read_limit_controls.py B (40 MiB + 12345: /dev/zero, a FIFO one byte over, a sparse file 3 × the limit, never exit 70); read_limit.py: every `exceed()` case's peak-RSS check, and `open()` of a regular file 3 × the limit |
| **Streaming reads** (larger than the limit, bounded per read) | read_limit.py: the `read_line` loop over a file 3 × the limit; `sha256_file` of a file 3 × the limit; the new VM `read(limit/2)` × 16 from /dev/zero; `ifstream(/dev/zero).read(huge)`; read_limit_controls.py C (paced short reads: a FIFO crossing the limit chunk by chunk) and D (`read(n)` counts the bytes read, not `n`) |
| **Memory budget and mutation controls** | read_limit_mutants.sh (8 mutants of the VM read natives: each must fail read_limit_controls.py; Linux campaign); vm_host_memory.py (`test-runtime2-host-memory`: VM host reads under `NIFT_VM_HEAP_BUDGET`, with peak trackers); the `test-rt-value` heap-budget and OOM units |

**What was run here:**
- read_limit_controls.py: PASS, on the pinned binary, earlier in this preparation.
- read_limit_contract_controls.py: 34 of 34 PASS, now.
- read_limit_mutants.sh and the full read_limit.py were not run here. They need a build, device cases and disk. They
  are Linux-campaign gates.

## Ruling applied

D-077 #3 (owner 2026-11-12 04:25) rejects a blanket change of the 24 to expected refusals. Each case is classified by
the invariant it is meant to test:

| Category | Disposition |
|---|---|
| Supported operation | Must execute and verify its invariant |
| Specifically deferred VM capability | The exact documented refusal may be expected; the capability is still counted as deferred |
| Safety invariant tested through unsupported syntax | Equivalent coverage through a supported path is required |
| Unapproved refusal | Stays FAILED |
| Cannot run here | NOT RUN, not PASS |

## How tests/runtime2/read_limit.py now works

- **Deferred capabilities** are listed in the `DEFERRED` table.

  | Refusal reason | Decision |
  |---|---|
  | `callable:run`, `callable:cmd` | D-065 #2, deferred from the initial release |
  | `builtin-bytes:open_bytes`, `callable:read` | Status `implement` (approved). Implementation is scheduled after the first certification baseline by D-077 #5 |

  Each such case runs twice:
  - **On the legacy channel (`NIFT_VM=0`).** This is the supported path that keeps the capability, and the read-limit
    invariant still lives there. The case keeps its original program and assertions; its name is the original
    name prefixed with `[legacy channel, NIFT_VM=0] `.
  - **As a VM refusal contract.** Every script the case ran is re-run under the VM default. It must:
    - exit with status 1;
    - print nothing on stdout;
    - say "no part of it ran";
    - give the exact documented first diagnostic,
      `case.f:L:C: not compiled for the VM (<reason>): <text>`.

    L:C is computed from the program text (the callee's position), never read from the VM's output. The case name
    is the original name prefixed with `[VM refusal contract, <reason>] `; the decision is printed in the summary.

  The summary prints `DEFERRED capabilities, checked only as VM refusal contracts (not evidence that they work on the
  VM)` with a count per reason. A clean run ends with "all read-limit checks passed (named deferred capabilities:
  refusal contracts only)".
- **Unapproved refusals keep their original VM-default case**, with the original name, so it stays FAILED and maps
  to the same certify case-ID table entry. These are `host-handle-flow`, `statement:Command` and `type:length`, all
  PENDING-OWNER, so none is an approved refusal. Their safety invariants keep coverage:
  - on the legacy channel;
  - where the VM supports an equivalent form, through that form:
    - `read(limit/2)` × 16 streaming stands in for `read_bytes`;
    - for the exact-limit `read_line` case, the line is printed and measured by the harness.
- **SKIPs are now NOT RUN.** The former SKIPs were for:
  - the default 1 GiB limit when MemAvailable < 4 GiB;
  - no `head`;
  - no `/proc/self/mem`;
  - no `/proc`, `yes`, `head` or `sh`.

  They now print `NOT RUN`. When nothing failed, the suite exits 2 with "N case(s) NOT RUN; this is not a pass".
  The default-limit refusal contract needs no memory, so it always runs.
- **Negative controls** are in `tests/runtime2/read_limit_contract_controls.py`, which runs in
  `test-runtime2-host-deps`. There are 34 controls:
  - each refusal contract must fail when the reason changes, when the callee moves by one column, or when the
    program runs instead (the legacy run);
  - each supported-path invariant must fail when the limit is removed. This is simulated by raising the limit past
    the input. The operations covered are open_bytes, read and run/cmd on the legacy channel; read_all_bytes,
    read_val and extraction on the legacy channel; and open and read_all on the VM.
- **Mutation controls.** The existing `read_limit_mutants.sh` mutates the VM read natives and runs
  `read_limit_controls.py`. It needs a build of the candidate's objects, so it was **NOT RUN** here. It does not
  mutate the legacy read path. No mutant of the legacy path exists: that would need a build, and a new mutation hook
  would be a production change (out of scope).

## The 24 cases, before and after

Before: C136/C137 `4bb09e9e` (identical to C129). After: pinned C137 binary, this repair. Evidence is in
`docs/runtime-2/evidence/host-deps-d077/`.

| # | Case (original name) | Refusal (before) | Category | After |
|---:|---|---|---|---|
| 1 | open_bytes: file of exactly the limit is read whole | builtin-bytes:open_bytes | deferred capability (D-077 #5) | legacy channel PASS; VM refusal contract PASS; counted deferred |
| 2 | open_bytes: empty file | builtin-bytes:open_bytes | deferred capability | legacy PASS; refusal contract PASS |
| 3 | open_bytes one byte over: fatal open_bytes limit error | builtin-bytes:open_bytes | deferred; limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 4 | open_bytes(/dev/zero): fatal open_bytes limit error | builtin-bytes:open_bytes | deferred; limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 5 | open_bytes(/dev/full): fatal open_bytes limit error | builtin-bytes:open_bytes | deferred; limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 6 | FIFO whose writer never stops (open_bytes) | builtin-bytes:open_bytes | deferred; limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 7 | default limit: open_bytes(/dev/zero) stops at 1073741824 bytes | builtin-bytes:open_bytes | deferred; limit invariant (needs MemAvailable ≥ 4 GiB) | refusal contract PASS; legacy PASS here (7.0 GiB available); NOT RUN where memory is short |
| 8 | read_line: line of exactly the limit | type:length | **unapproved refusal** (PENDING-OWNER) | **stays FAILED**; added: VM-supported equivalent (printed, measured) PASS; legacy channel PASS |
| 9 | ifstream(/dev/zero).read_all_bytes | host-handle-flow | **unapproved refusal** | **stays FAILED**; legacy channel PASS (and RSS) |
| 10 | ifstream(/dev/zero).read_bytes(huge) | host-handle-flow | **unapproved refusal** | **stays FAILED**; legacy channel PASS (and RSS) |
| 11 | ifstream(/dev/zero) >> token | statement:Command | **unapproved refusal** (the VM parses `s >> t` as a shell command line) | **stays FAILED**; legacy channel PASS (and RSS) |
| 12 | ifstream(/dev/zero).read_val | host-handle-flow | **unapproved refusal** | **stays FAILED**; legacy channel PASS (and RSS) |
| 13 | read_bytes(limit/2) x 16 from /dev/zero (8 x limit streamed) | host-handle-flow | **unapproved refusal** | **stays FAILED**; added: VM `read(limit/2)` × 16 PASS; legacy channel PASS |
| 14 | run(): capture of exactly the limit | callable:run | deferred (D-065 #2) | legacy PASS; refusal contract PASS |
| 15 | run(): capture one byte over | callable:run | deferred; limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 16 | cmd().run(): capture of 128 MiB | callable:cmd | deferred (D-065 #2); limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 17 | read(): stdin line of exactly the limit | callable:read | deferred (D-077 #5) | legacy PASS; refusal contract PASS |
| 18 | read(): stdin line one byte over is a limit error | callable:read | deferred; limit invariant | legacy PASS; refusal contract PASS |
| 19 | read(): endless stdin (/dev/zero) stops at the limit | callable:read | deferred; limit invariant | legacy PASS (and RSS); refusal contract PASS |
| 20 | run(yes) never ends | callable:run | deferred; capture bound and child kill | legacy PASS (limit, no producer left, RSS); refusal contract PASS |
| 21 | run(head -c 150000000 /dev/zero) | callable:run | deferred; capture bound | legacy PASS; refusal contract PASS |
| 22 | cmd(yes).pipe(cat).run() | callable:cmd | deferred; capture bound | legacy PASS; refusal contract PASS |
| 23 | run(sh -c 'yes; touch marker') kills the descendant | callable:run | deferred; descendant kill | legacy PASS (no marker, no producer left); refusal contract PASS |
| 24 | stderr capture of an endless producer | callable:run | deferred; capture bound | legacy PASS; refusal contract PASS |

**Totals:**
- 18 deferred-capability cases: refusal contracts PASS, legacy-channel invariants PASS. Counted as deferred:
  open_bytes 7, run 6, cmd 2, read 3.
- 6 unapproved refusals: **still FAILED**. These are `host-handle-flow` 4, `statement:Command` 1 and `type:length` 1.
- 0 NOT RUN on this host.

The gate therefore stays **FAILED** (6 failures) until the owner rules on the three PENDING-OWNER reasons. The P1
inventory reconciliation leaves `statement:Command` and `type:length` open; `host-handle-flow` is a PENDING-OWNER row
that is not seen on the corpora.

**Run here:**
- `read_limit.py` on the pinned C137 binary, through a scratch driver that skips any script opening a device in
  `"w"` mode, to keep this sandbox's devices safe. The result was 142 PASS and 8 FAIL: the 6 above, plus the two
  guarded `file(/dev/zero|/dev/full).open(w)` cases. Those two are not in the 24, and C136 passes them.
- `read_limit_controls.py`: all passed.
- `read_limit_contract_controls.py`: 34 of 34.

## Notes for the reviewer and the owner

- **`host-handle-flow` may be mislabelled.**
  - `s.read_bytes(4)`, `s.read_all_bytes()` and `s.read_val()` on a regular file are refused as `host-handle-flow` at
    the call. `read(n)`, `read_all()` and `read_line()` run.
  - The reason text ("this flow of host handles") does not describe a bytes or value result.
  - An owner decision is needed: approve, defer by name, or implement.
- **`s >> t` is refused as `statement:Command`.** The VM classifies a stream-extraction statement as a shell command
  line. Whatever the decision on `statement:Command`, this looks like a misclassification worth its own row.
- **Certify fingerprints and case IDs.** `tests/runtime2/certify-fingerprints/test-runtime2-host-deps.txt` is
  historical: a C26 run with 33 FAILs that contains the 24. It is **unchanged**; the new evidence is added beside it,
  in `docs/runtime-2/evidence/host-deps-d077/`.
  - **Correction (review F-P2-1, condition C4).** The first version of this page said that a new failing name was
    UNMAPPED in the approved case-ID table. That was wrong. `scripts/certify_fingerprints.py` matches a table entry
    on a name *prefix followed by a space*, so the original suffix tags (`… [legacy channel, NIFT_VM=0]`,
    `… [VM default: refusal contract, …]`, `… (printed, measured by the harness)`) mapped onto the **original**
    case IDs.
  - **Fix: the tags are now prefixes**, and the two VM-equivalent cases carry `[VM equivalent form] `. A tagged line
    starts with `[`:
    - no entry with fixed leading text matches it;
    - an entry that starts with a name group captures the tag, so the case ID differs from the original's;
    - the remaining tagged lines map to no entry and are UNMAPPED, which blocks on its own text.
  - **`tests/runtime2/read_limit_names_selftest.py`** proves this on the real table, in `test-runtime2-host-deps`.
    It takes every case name of the recorded run and has read_limit.py's own `check()` (legacy channel) and
    `refusal_contract()` print each tagged form, so no name is hand-built. It then maps each FAIL line with certify's
    own `certify_fingerprints.fingerprint()` (review L-a). Mutating `check()` or `refusal_contract()` back to the
    suffix form makes it fail (shown). It requires that none is ambiguous, none maps to an original ID, and the forms of one case
    never share an ID. As a negative control, the old suffix form must collide (it does, for all 130 names).
    Without `scripts/` (a sparse checkout) the selftest is NOT RUN (exit 2).
  - **Against the weekend/r2 baseline** the FAIL set is 6 instead of 24, so certify's `report_class` (equal
    fingerprints required) gives **FAILED-NEW**, not FAILED-BASELINE, until the 6 are ruled on. That is the honest
    result.
- **The `implement` status of `builtin-bytes:open_bytes` and `callable:read` is a judgement call.** They are not
  individually named deferrals; they are treated as deferred under D-077 #5 ("All other approved feature
  implementation remains deferred until after the first certification baseline"). If the owner rules otherwise,
  their 10 cases return to FAILED: replace their `deferred(...)` wrappers with the original VM-default cases plus
  `legacy_channel(...)`, as is already done for the unapproved group. That is a small test change.

## Open owner questions (review conditions)

- **C3.** An explicit owner line deferring `open_bytes()` and `read()` by name until after the first certification,
  or their 10 cases revert to FAILED (see the previous note).
- **C5.** How the 18 refusal-contract cases are reported as APPROVED DEFERRAL. certify has no such class: once the 6
  unapproved refusals are resolved, the gate classifies PASSED. The certification report must therefore take the
  deferral count from read_limit.py's `DEFERRED capabilities, checked only as VM refusal contracts …` line (per
  reason: open_bytes 7, run 6, cmd 2, read 3) and from the inventory, and list them under "named deferred
  capabilities", never under passing functionality. The proposed rule is in certification-r2-plan.md, section 3.
