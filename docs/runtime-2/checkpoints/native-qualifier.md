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
  the same text at the same position. `::` inside strings and comments is ordinary text.
- **VM-default execution** (script files, `-e`, `-c`, stdin, `-i`, the REPL, and imported modules): the unqualified
  seven are not built-ins. A user or module function with one of these names is an ordinary function. Otherwise an
  unqualified call refuses the whole program at compile time (`native-unqualified`), reached or not, before anything
  runs. The diagnostic adds `note: Runtime 2.0 calls them as native::name(...), for example native::cp("a", "b"); a
  user function of that name is an ordinary function`. In a session the refused statement is handled like any other
  session refusal. A function call never launches an external executable: the call is a native or a user function,
  or it is refused.
- **Legacy** (`NIFT_VM=0`, the historical oracle) keeps the unqualified seven unchanged and also accepts
  `native::name(`.
- **Templates and build hooks are unchanged** (D-096 #1: the initial L6 boundary). `@script`, `$[...]` and hook
  scripts run on the legacy evaluator (D-053), so they keep the unqualified seven. This is transitional
  engine-specific compatibility, not the permanent API. `native::name(` works there too, with the same output and
  effects as the unqualified form.
- **An unknown name, by path (D-095 (b)).** The CP13 syntax prevalidation checks every script before engine selection,
  so under the default it refuses the program before anything runs, the same way in both engines. Where
  prevalidation does not run (`NIFT_PREVALIDATE=off`, and template code, which it does not cover), both engines raise
  the same uncatchable error at the same position when the call is reached. Both paths are tested
  (`unknown-print`/`unknown-copy`/`unknown-foo` and `unknown-unvalidated`).

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
  versions time `nift run <script>`, which this candidate and v4.12.0 both reject ("unknown command 'run'"), so
  neither runs its scripts;
- the website checkout.

The freeze report lists every benchmark and workload whose VM-default run is now refused.

The approved legacy/VM difference for a user function named like one of the seven is pinned in
tests/runtime2/l6/adversarial-approved.json: nine generated programs, each by source hash, with the exact results
of both engines.
