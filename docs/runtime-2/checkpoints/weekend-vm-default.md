# Weekend integration: strict VM default (cand/vm-default)

Status: **integrated into local `weekend/r2` under an explicit owner exception**
(2026-10-31). Not independently accepted. `runtime-2` not advanced; nothing pushed.

- Base: `1512579f` (runtime-2 = origin/runtime-2 = weekend/r2 start).
- Candidate: `cand/vm-default`, `0503062d..71bb0056` (22 commits), frozen at `71bb0056`.
- Merge into `weekend/r2`: merge commit with parents `1512579f`, `71bb0056`; tree identical to `71bb0056`.
- Contract: `tests/runtime2/vm_default/REFUSAL_MATRIX.md`; decision D-061 (startup `exit`).

## Independent review ledger

| Review | Tip | Verdict | Principal findings |
|---|---|---|---|
| 1 | b61784a2 | REJECT | (see candidate history) |
| 2 | b8a5da28 | REJECT | (see candidate history) |
| 3 | f96be6ef | REJECT | (see candidate history) |
| 4 | 50ba8db6 | REJECT | shell-form `\|\|` ran after refusal; earlier-unit location; startup refusal order |
| 5 | 5e2604c8 | REJECT | `$[...]` location ignored refusal unit; tty startup file compiled whole |
| 6 | be1b3d09 | REJECT | tty startup comments run as shell commands; if/else split; value echo; invented `:1:1` |
| 7 | fb330b4c | REJECT | positionless refusal at a 1:1 statement printed as exact `:1:1`; syntax-error interrupted line |
| 8 | 71bb0056 | **REJECT** | see below |

### Review 8 findings (71bb0056)

- **MEDIUM (diagnostic only).** Positionless whole-program refusals still print an
  invented exact location on three paths. Exit status 1 and "no part of it ran" are
  correct; side effects are not misrepresented.
  1. `coro-module`: `compile_unit_impl` marks every `check_coroutine_unit` failure
     positioned, but `compiler_coro.cpp` sets `offset = ast.at(root).begin` for this
     whole-unit refusal (e.g. `main.f:1:1` on a comment line).
  2. `import-name-clash`: `module_link.cpp` refuses at the module root start and
     `Stats::positioned` defaults to true (e.g. `lib.f:1:1`, a comment).
  3. import depth limit: `module_graph.cpp` `refuse(index, 0, ...)` (e.g. `m63.f:1:1`;
     the import is at 2:3).
- **LOW.** D-061 point 2 says non-tty startup is unchanged "as in legacy"; piped legacy
  runs earlier statements before failing, the VM runs none. Only the exit status matches.
- **LOW (cosmetic).** The quoted startup statement can include a trailing comment
  (`exit // bye` -> ``(`exit // bye`)``); still stops the file, prompt usable.
- Confirmed: session-path has-position handling; genuine `:1:1` kept; D-061 behaviour on
  both tty startup paths; piped `exit` refused before effect with exit 1 on both paths;
  the six `src/CLI.cpp` lines of 71bb0056 only build the note string and cannot affect
  execution or session cleanup.

## Owner exception (2026-10-31, "Option 1: merge now, fix after")

An owner-authorised exception to the Review 8 REJECT for local weekend integration,
not a retrospective independent acceptance. The strict VM default is functionally
ready to build on.

Open obligations, **release-blocking** for public Runtime 2.0 (not blocking weekend
integration or feature work):

1. Positionless `coro-module` refusals must not report an exact location.
2. Positionless `import-name-clash` refusals must not report an exact location.
3. Positionless import-depth-limit refusals must not report an exact location.
4. Correct D-061's piped-startup legacy-parity wording.
5. Trailing comment in the quoted statement, only if the fix stays small.

These go in a separate narrow candidate on `weekend/r2` using the explicit
has-position contract (never inferred from offsets), with tests distinguishing a
genuine `:1:1` from an unavailable position, a full `test-rt-vm` on its exact tip, and
focused independent review before integration.

## Evidence

- Full `test-rt-vm` passed on `56434245` (3255 s). Not rerun on `71bb0056`; Review 8
  judged the remaining `src/CLI.cpp` change unable to affect execution or cleanup.
- On `71bb0056` (binary confirmed built from the tip): `make nift`,
  test-runtime2-vm-default, cp17-tripwire, cp22, cp19-functional, cp19 session tests
  (24/24), cp25r, cp23, test-cp23-cp19-combined, test-site-dx, test-content,
  test-template-optional, test-template-variables, test-pagination,
  test-markup-json-directives, test-v410-build-pipeline: all exit 0.
- Mutation controls: shell classifier, whole-file compile, double execution, echo,
  1:1 fallback, inferred position, offset default, syntax-error note line: each fails
  the VM-default suite.
- Known limits recorded in `REFUSAL_MATRIX.md` (column drift after quotes in `$[...]`,
  cautious non-tty wording, bare expressions refused, piped `exit` reason ids differ).
