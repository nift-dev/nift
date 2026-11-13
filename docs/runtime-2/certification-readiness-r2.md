# Runtime 2.0 certification readiness (R2), 2026-11-12

**This is a readiness report, not a certification result.** No certification campaign has run on this candidate: no
Linode, CI, push or tag. Every result below is local evidence on the pinned C137 binary (sha256
`4bb09e9e…41c0b909c4`), or a status taken from the records.

The report follows D-077 and D-078 (owner 2026-11-12, attachments 83af45ff and 2ea1c509). The preparation branch is
`prep/cert-r2`. Certification is not release authorisation.

## 1. Headline

- **Not ready to certify. The four approved production checkpoints are not done** (section 6).
- **The inventory gate does not complete.** The tool prints `INVENTORY: BLOCKED: 11 PENDING-OWNER reason(s), not adjudicated: …` (exit 3). This report classifies that as **"Inventory incomplete: 11 open reasons"** (section 3).
- **`test-runtime2-host-deps` is FAILED, with 6 outstanding cases** (section 4).
- **The required certification guard (`test-runtime2-certification-guard`, D-090) is FAILED** (since C140c). It is expected to PASS once the candidate fix lands.
- **The mandatory sanitizer, mutation, toolchain, cross-platform and performance gates are NOT RUN.**
- **Deferrals do not change any of this.** A deferral never turns a failed mandatory execution test into a PASS
  (D-078 #5). A refusal-contract pass shows that a deferred capability is refused in the approved manner, never that
  it works.

## 2. Classes (D-078 #5)

| Class | Meaning |
|---|---|
| PASS | the supported capability executes correctly |
| APPROVED DIFFERENCE | an intentional, reviewed semantic difference |
| DEFERRED — IMPLEMENTATION | a valid capability intentionally postponed |
| DEFERRED — COMPATIBILITY | a known compatibility gap postponed |
| FAILED | a required test or safety contract is not satisfied |
| PARTIAL | some required checks were not executed |
| NOT RUN | no execution evidence |

## 3. CP25-R refusal inventory (137 reasons seen on the release corpora)

The pinned C137 binary was collected with `inventory.py collect`: 788 units (588 excluding generated) and 607
execute entries. `report --check` exits 3: G1 PASS, and 11 PENDING-OWNER reasons block. Each reason is counted
exactly once:

| Class | Reasons | Statuses |
|---|---:|---|
| PASS (implemented and accepted; seen only as slot-3 deferred sites) | 3 | implemented-accepted |
| APPROVED DIFFERENCE | 14 | approved |
| DEFERRED — IMPLEMENTATION, named | 61 | deferred-named 27 (D-065, D-067 #2: run/cmd, FFI, timers, the CP24 project and template APIs, delayed and dynamic imports); deferred-post-cert 34 (D-077 #1, D-078 #1-#3) |
| DEFERRED — IMPLEMENTATION, approved for implementation and scheduled after certification (D-076, D-077 #5), reported separately (D-078 #5) | 30 | implement |
| DEFERRED — COMPATIBILITY, named | 18 | deferred-named 2 (legacy dynamic scope: `free-name`, `free-name:dynamic-caller`); deferred-post-cert 16 |
| FAILED: open, unresolved, no decision | **11** | PENDING-OWNER |
| **Total** | **137** | |

**No double counting.** The original statement spoke of 32 approved-but-deferred rows. Two of them, `open_bytes()`
and `read()`, are now named deferrals (D-078 #2). They appear only there, with their implementation approval kept as
history, so 30 remain `implement`.

**Inventory status.** Tool output: `INVENTORY: BLOCKED: 11 PENDING-OWNER reason(s), not adjudicated: …` (exit 3). Report classification: **Inventory incomplete: 11 open reasons** (D-078 #3: they stay OPEN, and none was converted):
1. `callable:minify`: no deferral names it.
2. `coro-async-value`: coroutine capability, not deferred.
3. `method:catch`: masked by `opaque:legacy-runtime-error`.
4. `method:error`: same.
5. `method:throw`: same.
6. `method:sort_by`: a non-masked sighting in the sort-factory gate.
7. `method:to_string`: mixed roots, including an open one.
8. `opaque:legacy-runtime-error`: a catch-all.
9. `shebang-statement`: a v4.4 behaviour that the smoke suite asserts.
10. `statement:Command`: shell command lines, plus finding F-HD2.
11. `unresolved-name`: a catch-all.

**The 30 `implement` rows:**
- `binding-name-reserved`
- `builtin-bytes:bytes`, `builtin-bytes:decode`, `builtin-bytes:encode`
- `builtin-receiver:to_string`
- `callable:err`, `callable:exit`, `callable:warn`
- `context:struct-new`
- `declare-pattern`
- `export-struct`
- `expression-statement`
- `fn-called-before-declaration`
- `fn-param`
- `import-collision`
- `index-target`
- `lambda-variadic`
- `method:decode`, `method:encode`
- `path-in-call-argument`
- `postfix-spelling`
- `spread`
- `statement:Export`
- `static-error:arity`, `static-error:member-assign`, `static-error:struct-arity`
- `string-arg-type`
- `struct-after-statement`, `struct-field-init`, `struct-private`

Evidence: tests/runtime2/cp25/classification.tsv, post_cert_deferrals.tsv (with `report_class` per deferral),
inventory-post-cert-deferrals.md.

**Refusal contracts of the 50 deferred-post-cert reasons:**
- `post_cert_refusals.py`: PASS, 94 scoped units.
- Negative controls:
  - `root`: 50 of 50 detected;
  - `position`: 50 of 50 detected;
  - `unlisted-position`: 11 of 11 pinned entries detected.

  The pinned entries are `alias-argument`, `type:push`, `method:push` and the 8 union-kind rows. A new push or
  union-kind sighting in a scoped unit fails the contract. A sighting in any other unit blocks `--check`.
- These count as DEFERRED, not PASS.

**Dependency (review L2).** The pinned sightings (`@L:C` in a scope) are enforced **only** by
`post_cert_refusals.py`; `inventory.py --check` strips the positions and checks units and roots only. So
`make cp25r-inventory` now runs `post_cert_refusals.py` before `--check`, and `test-cp25r` runs it as well; neither
inventory gate runs without it.

## 4. Host-dependency suite: FAILED

`make test-runtime2-host-deps` is **FAILED**. Its 6 outstanding cases stay FAILED (D-078 #4):

| Case | Emitted reason | Classification |
|---|---|---|
| ifstream(/dev/zero).read_all_bytes | host-handle-flow | stream byte read. **F-HD1:** the reason names the compiler's generic fallback, not the operation |
| ifstream(/dev/zero).read_bytes(huge) | host-handle-flow | stream byte read (F-HD1) |
| read_bytes(limit/2) x 16 from /dev/zero | host-handle-flow | stream byte read (F-HD1) |
| ifstream(/dev/zero) >> token | statement:Command | stream extraction. **F-HD2:** a `>>` statement is classified as a shell command |
| ifstream(/dev/zero).read_val | host-handle-flow | value read, refused by the same generic host-handle-flow fallback as the F-HD1 byte reads; whether it belongs under F-HD1 is not established |
| read_line: line of exactly the limit | type:length | union-kind `.length()`; outside the pinned D-078 #3 deferral |

- **F-HD1 and F-HD2 are recorded as findings.** Correcting either reason ID needs a production diagnostic change,
  which is not made here.
- **18 cases are named deferrals:**

  | Capability | Cases | Decision |
  |---|---:|---|
  | `open_bytes()` | 7 | D-078 #2 |
  | `read()` | 3 | D-078 #2 |
  | `run()` | 6 | D-065 #2 |
  | `cmd()` | 2 | D-065 #2 |

  - Each runs its original invariant on the legacy channel and a VM refusal contract.
  - **Counted as DEFERRED — IMPLEMENTATION**, never PASS.
  - The B01 read-limit, bounded-allocation, streaming and memory-budget coverage keeps executing through supported
    VM paths; the list is in host-deps-test-contract.md.
- **Local evidence:**
  - read_limit.py (guarded driver, earlier in this preparation): the 6 above FAIL. The device-write cases
    `file(/dev/zero|/dev/full).open(w)` were **NOT RUN** here, by the hard rule against writing to /dev; they pass in
    C136.
  - read_limit_controls.py: PASS.
  - read_limit_contract_controls.py: 34 of 34.
  - read_limit_names_selftest.py: PASS.

## 5. Gates

| Gate | Class now | Evidence or reason |
|---|---|---|
| `make test-runtime2-certification-guard` (required gate, D-090) | **FAILED** | It has failed unnoticed since C140c: three skip phrases in tests/runtime2/suite_status.sh were never listed. A fix is being prepared on the candidate. **Expected PASS once it lands**, and it must then pass on the frozen candidate. Not run in this sparse worktree |
| `make test-rt-vm` | PASS (local, C136 binary; the source is unchanged at C137) | uninterrupted run, rc 0 (REVIEW-C136-SLOT3). It must be re-run on the frozen candidate |
| CP25-R inventory (`cp25r-inventory`) | **FAILED** | tool: `BLOCKED: 11 PENDING-OWNER reason(s)`, exit 3. Classified as: Inventory incomplete, 11 open reasons |
| D-077 #1 refusal contracts (`test-cp25r`, the post_cert_refusals part) | PASS (a refusal contract; the capabilities are DEFERRED) | section 3 |
| `test-runtime2-host-deps` | **FAILED** | 6 outstanding cases (section 4) |
| `test-runtime2-read-limit-mutants` | NOT RUN | needs a build (Linux campaign) |
| Toolchain preflight (GCC 15.2.0-16ubuntu1, `__GLIBCXX__` 20260321) | NOT RUN | Linode only |
| MANDATORY B3-B5, refsem, cp22 and cp23 gates on GCC 15.2 | NOT RUN | Linode |
| Sanitizer tier (ASan/UBSan/TSan) | NOT RUN | Linode only |
| Mutation tier | NOT RUN | Linode |
| Performance lane | NOT RUN | Linode |
| Website-build benchmark vs v4.12.0 | NOT RUN | Linode; needs approval |
| Regression suite (`runtime-2`) | NOT RUN on this candidate | the 4 `cat`/`ls`/`touch` contracts need a decision after L6 |
| Cross-platform CI (incl. runtime2-cross-platform) | NOT RUN | needs push and dispatch approval; action SHAs to pin |
| FreeBSD | out of the first certification | D-077 #4 |
| PARTIAL | none observed | no campaign has run. The subset runner reports PARTIAL for any unexpected skip |

## 6. Production checkpoints remaining: NOT DONE

| # | Checkpoint (D-077 #5, D-078 #6) | Status |
|---:|---|---|
| 1 | Output-order and std-stream data-loss repair | **not done** (implementer in progress) |
| 2 | Async recursion memory safety (CP23 `qualified:legacy-await-chain-defect` open) | **not done** |
| 3 | `save()` special-file and symlink safety | **not done** |
| 4 | L6 `native::` Option B | **not done** |
| 5 | Freeze, then cross-platform certification | not started |

## 7. Open decisions and findings

- **The 11 open inventory reasons** (section 3). They keep the inventory gate from completing.
- **The 6 host-deps cases**, and findings **F-HD1** and **F-HD2** (production diagnostics).
- **C1:** the integrator records the D-076 defer table (attachment 3d29daee) and D-078 in decisions.md at
  integration.
- **certify.py was not changed.** No concrete misreporting defect was found. certify has no DEFERRED class, so a gate
  whose deferred content consists only of refusal contracts classifies PASSED. This report counts those
  capabilities as DEFERRED from the harnesses' own lines (read_limit.py's `DEFERRED capabilities …` line,
  `post_cert_refusals.py`, the inventory), which is the rule in certification-r2-plan.md section 3.
- **Before any launch:** pin the cross-platform workflow's action SHAs. Run every locally runnable gate on the frozen
  candidate (the owner's standing rule).
