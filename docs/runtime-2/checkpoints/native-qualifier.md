# `native::` for seven file built-ins (L6 Option B; production checkpoint)

**Status:** implemented locally (C143 `ba0895e7`, C143a `b0248519`, then the migration and records commits); pending
freeze review and owner acceptance. Owner decisions D-042 and D-044 #2 (the `native::` spelling), D-075 #2 (Option B
approved), D-095 (L6 authorised; the design approved with answers (a) to (c) and conditions 1 to 8).

## Contract

- Exactly seven functions take the qualifier: `cat`, `cp`, `mv`, `rm`, `ls`, `mkdir`, `touch`. `native::name(` calls
  the existing native of that name in both engines, with every check the unqualified native had: arity and argument
  checks, `--fs-root`, read limits and memory accounting, host-call tracing, dependency observations, module-import
  effect rules, admission and thread/async restrictions. There are no namespace objects or values and no general
  namespace mechanism.
- The only spelling is `native::name(`: no space around `::`, and the `(` immediately after the name. `native :: cp(`,
  `native::cp (`, `native::  cp(`, `native::cp` without a call, `x.native::cp(` and `native::native::cp(` are
  reported as `malformed native:: call: write native::name(...)`. `native::` before any other name is reported as
  `native::NAME is not a native function (native:: names cat, cp, mv, rm, ls, mkdir and touch)`. Both engines report
  the same text at the same position wherever the prevalidator checks the program (see the unknown-name item
  below for where it does not). `::` inside strings and comments is ordinary text.
- **VM-default execution** (script files, `-e`, `-c`, stdin, `-i`, the REPL, and imported modules): the unqualified
  seven are not built-ins. A user or module function with one of these names is an ordinary function. Otherwise an
  unqualified call refuses the whole program at compile time (`native-unqualified`), reached or not, before anything
  runs. The diagnostic adds `note: Runtime 2.0 calls them as native::name(...), for example native::cp("a", "b"); a
  user function of that name is an ordinary function`. In a session the refused statement is handled like any other
  session refusal. Function-call syntax never implicitly launches an external executable: the call is a native or a
  user function, or it is refused. This concerns implicit dispatch only; the explicit process APIs `run()` and
  `cmd()` launch programs by design (D-101 F4). Until C144 an interactive or piped session classified a call written
  with a blank before `(` as a shell command line (F1, the CP19 S1 classifier); C144 corrects it
  (checkpoints/cp19-s1-dispatch.md).
- **Legacy** (`NIFT_VM=0`, the historical oracle) keeps the unqualified seven unchanged and also accepts
  `native::name(`.
- **Templates and build hooks are unchanged** (D-096 #1: the initial L6 boundary). `@script`, `$[...]` and hook
  scripts run on the legacy evaluator (D-053), so they keep the unqualified seven. This is transitional
  engine-specific compatibility, not the permanent API. `native::name(` works there too, with the same output and
  effects as the unqualified form.
- **An unknown name, by path (D-095 (b)).** The CP13 syntax prevalidation checks every script before engine selection,
  so under the default it refuses the program before anything runs, the same way in both engines. Where
  prevalidation does not run, the engines refuse alike but report differently (D-101 F2, F3; known-limitations.md):
  - `NIFT_PREVALIDATE=off`: the VM refuses the whole program; legacy runs the earlier statements, then errors at the
    call. A top-level unknown name has the same text and position (tested: `unknown-unvalidated`); a nested one has
    the same text, but the VM points at the call and legacy at the statement.
  - `native :: cp(` and template code: legacy's text is its own "unknown value or malformed expression".
  - Prevalidation misses a bad spelling in a `$[...]` segment, `native` and `::` on separate lines,
    `fn(native::cat(p))`, and session statements; the VM refuses each, legacy errors when it reaches it.
  In none of these does a wrong native run or a program launch. The default path is tested by
  `unknown-print`/`unknown-copy`/`unknown-foo`.

## Implementation (src, C143 +196/-4, C143a 1 line)

| File | Change |
| --- | --- |
| src/rt/native_qualifier.h (new) | the seven, `native::`, and the spelling checks shared by the prevalidator (token and AST rules) |
| src/rt/lexer.cpp | `native::name` becomes one identifier token only when `(` follows immediately |
| src/rt/builtin_bind.cpp | `bind_function`: a qualified name binds the existing native id (the seven only); the unqualified seven bind nothing. Every name-based analysis goes through it |
| src/rt/compiler_thread.cpp | the whole-program refusal `native-unqualified` (the C136 undefined-callables path) |
| src/rt/compiler_deferred.cpp | a qualified unknown name is a deferred uncatchable raise (the unvalidated path) |
| src/rt/resolver.cpp | the seven are no longer reserved function names |
| src/rt/expr_postfix.cpp | the receiver test skips the qualifier, so `native::ls(".").length()` is a call then a method |
| src/rt/prevalidate.cpp | the AST rule (a qualified identifier only as a call's callee, only the seven) and the token rule in opaque regions |
| src/rt/refusal_text.cpp | the `native-unqualified` description and the migration note |
| src/ParserExpression.cpp | legacy: `native_unqualified_text` rewrites `native::name(` to `name(` plus eight spaces before evaluation. The rewrite keeps the length, so positions do not change. It respects quotes, stops at a trailing comment, and reports a bad spelling or an unknown name |

**Legacy scope (condition 1).** Legacy needs no flag. Its native heads already take precedence over user, module and
host callables (`canonical_native_call`), and the rewrite changes only the qualifier text of the outer callee.
Arguments and nested calls keep their ordinary meaning, and no state remains after the call that could leak
(`arguments-and-nesting`, `prepared-loop`, and mutant 5).

**Same native id (no bypass).** `NIFT_VM_DUMP=1` shows `native::cp("a.txt", "b.txt")` compiling to `CallNative 1 106
2`, the native id that unqualified `cp` had before L6. The freeze report compares it with a build in which the
unqualified binding is kept (`NIFT_L6_MUTANT=2`).

## Tests and mutants

`tests/runtime2/native_qualifier.py` (`make test-runtime2-native-qualifier`, and a test-rt-vm recipe suite): VM and
legacy for each case. The cases are:
- the seven;
- the refused unqualified forms in five placements (top level, unreached, function, lambda, async), with a scratch
  PATH whose `cat`, `cp`, ... must never run;
- user functions with each of the seven names;
- unknown names and the spellings listed above;
- strings and comments, and `native` used as a binding;
- arguments and nesting, error positions, the prepared loop, deferred name certainty and read limits;
- threads and async, traces, and module host order;
- sessions, a site template and hook with their dependency observations, and a trailing comment;
- the unvalidated path;
- D-096: a user function of each name under the VM default, called and also present in an unreachable branch
  (`user-function-default-*`); `native::name` with a user function of that name, which still reaches the native in
  both engines (`shadowed-qualified-*`); and legacy file, `-e` and `@script` runs of `native::` that equal the
  unqualified form (`legacy-equivalence-script`, `legacy-equivalence-template`);
- the embedding API (`native_qualifier_embed.cpp`, built in scratch and removed afterwards).

The production binary must pass every case (the no-mutation control). Then each mutant is built with
sanctioned_mutant.py and removed afterwards:

| Mutant | Defect | Caught by |
| --- | --- | --- |
| 1 | the VM qualifier binds any name | `vm unknown-unvalidated` |
| 2 | the unqualified seven stay bound on the VM | `vm unqualified-*-{top,unreached,function}` |
| 3 | no prevalidation rule | `both spelling-spaced`, `both spelling-no-call`, `both unknown-print` |
| 4 | the seven stay reserved function names | `vm user-function-*` |
| 5 | the receiver test reads `::` as an operator | `both arguments-and-nesting` |
| 6 | legacy accepts any qualified name | `legacy unknown-unvalidated` |

## Migration

The repository call sites that the 146-target discovery run at `ba0895e7` proved need the change are migrated. Each
changed line is listed in tests/runtime2/l6/MANIFEST.tsv with its reason. The criteria are:
- a gate failed because of the site;
- or a gate's own report showed that a program run on the VM before L6 now fell back on `native-unqualified`.

Template and hook code was left unqualified. Recorded oracle data (tests/data/*.json) was left as recorded; the test
qualifies the program at run time instead. An optional older-release baseline (`NIFT_BASELINE`) keeps running the
original unqualified program.

These were not changed:
- the frozen evidence (docs/evidence/cp410-competitiveness/probes/filesystem-{2000,4000,8000}.f, which now fall back
  under rt-diff's `NIFT_VM=1`);
- benchmarks/v43_scripting_perf.py, which stays byte-identical (D-096 #2); its Runtime 2.0 variant
  benchmarks/v43_scripting_perf_rt2.py changes only `touch(` to `native::touch(` in fs.nift and reports
  identical-source and equivalent-operation workloads separately (`--check-equivalence` gives the evidence). Both
  versions time `nift run <script>`, which this candidate and v4.12.0 both reject ("unknown command 'run'"). So
  both variants currently time that error path, on both versions; only the `nift sh` row runs its input. Neither
  variant's numbers are evidence of `native::touch` performance, and no comparison from them may be reported until
  the owner decides (the benchmark is not repaired here). `--check-equivalence` also makes an untimed direct run
  (`nift <script>`), kept separate from the timed command line, to compare the operations themselves;
- the website checkout.

The freeze report lists every benchmark and workload whose VM-default run is now refused.

**Criterion-2 sites left unqualified (D-101 F8).** Three sites fell back on `native-unqualified` in the discovery run
but were left unqualified, because a qualified copy still does not run on the VM. They were never committed (the
qualification was undone in the working tree before the migration commit), so no commit records them. They can be
checked at the frozen tree: each is still unqualified, and a scratch copy with `native::` added is refused for
another reason under `NIFT_VM=require`:
- tests/runtime2/rt-diff-corpus/scripts/trace-coverage/main.f: `callable:cmd` (17:6);
- tests/runtime2/rt-diff-corpus/workspaces/script-outside/project/main.f: `module-host-op:cat` (1:7);
- tests/site_dx_smoke.sh, the `arity.f` heredoc (line 124): `static-error:arity` (5:5).

The approved legacy/VM difference for a user function named like one of the seven is pinned in
tests/runtime2/l6/adversarial-approved.json: nine generated programs, each by source hash, with the exact results
of both engines.

## Performance and gate notes (D-101)

- **Instruction counts.** A callgrind run of `nift build --all` on the dogfood sites siteA and siteB-1k compared the
  candidate with a rebuild of 502875c4 (two repetitions, alternating). It shows **no measurable instruction-count
  change**: every candidate-vs-base delta was smaller than the base-vs-base noise of about 0.06% (REVIEW-L6 section 8).
  Website builds take the legacy path, where L6 adds only the `native::` text scan.
- **Gate records (F7).**
  - test-v410-native-dispatch-parity exits 2 with the same 3 failures as before L6 (`callable:to_string`,
    `callable:unknown` and `static-error:arity`, all `implement`); its new L6 case passes.
  - test-v45-native-runtime stops at its first failing sub-target (`callable:ffi_open`, deferred-named), so its later
    sub-targets (adversarial-runtime, concurrency, job-control with its cp21 prerequisites, and target) did not run in the freeze
    gate set; their scripts contain no calls to the seven.
  - The rt-diff corpus entry trace-coverage is now refused first with `native-unqualified`. Its `callable:cmd`
    refusal is still counted by the other corpus entries that call `cmd()`.
