# CP19 Slice 1 — classify-before-execute shell dispatch (D-042)

**Owner:** approved 2026-10-21 (P1-b command-form resolution)
**Base:** `runtime-2` @ `846da5c16ff0dc669f6e16bcbf3cedf406b77494`
**Branch:** `cp19-s1-dispatch` (local only; not pushed or integrated)
**Reviewer/certifier:** Claude

## Scope

This slice removes the shell's speculative “try a command-style Nift callable,
then retry the whole line externally” behaviour. A command-form line is
classified once, purely, before any Nift code or external process runs.

It does **not** move Nift statements, user functions, `print` or `pwd` to the
bytecode VM. Those still execute through the legacy evaluator. VM execution of
the shell front end remains a later CP19 requirement.

## Classifier

New module: `src/shell/CommandDispatch.{h,cpp}`. It is pure: it receives the
tokenised line and a read-only snapshot (shell built-ins, parser-visible user
functions/bindings, the `print`/`pwd` command allow-list, and a PATH predicate)
and returns one of:

`NiftStatement`, `ShellBuiltin`, `UserFunction`, `NiftBuiltin`, `External`,
`NotFound`.

No classification path calls the parser, starts a process or touches the
filesystem.

## Precedence implemented (P1-b)

1. Not command form → Nift statement (function-call syntax, assignments,
   braces, declarations, literals in a standalone line).
2. Shell-state commands: `cd`, `exit`, `quit`, `jobs`, `fg`, `bg`, `wait`.
3. User-defined functions (parser-visible callables/bindings); called once
   with string arguments.
4. Command-form Nift built-ins: **exactly `print` and `pwd`**. Both are
   read-only.
5. `PATH` executables and explicit paths (`./x`, `/x`, `dir/x`).
6. Otherwise `nift: <name>: command not found`, status `127`.

`command name …` bypasses steps 2–4 and selects external resolution only.

Filesystem and process built-ins do **not** intercept command form. `ls`,
`cp`, `mv`, `rm`, `cat`, `mkdir`, `touch`, `copy`, `remove` and every other
non-allow-listed built-in resolve through `PATH` (or report 127).

## Exactly-once guarantees

- Classification is final; a failed user function or allow-listed built-in is
  never retried externally.
- A failed external command is never retried through the Nift evaluator.
- Classification happens once, up front. `$[expr]` is evaluated only when its
  segment is actually selected, immediately before that segment dispatches.
  Skipped segments are never interpolated. Executed segments evaluate each
  interpolation exactly once, left to right. Failure sets status 2 and does not
  dispatch; it never reclassifies or falls back. An interpolation that changes
  `PATH`, `cwd` or other state cannot change the mechanism chosen up front.
- Quoting and escaping are preserved through tokenization. Only unquoted `*`,
  `?` and `[` trigger globbing; quoted/escaped metacharacters are passed
  literally. Only unquoted `&&`, `||`, `;`, `|`, `<`, `>`, `>>`, `2>`, `2>>`,
  `2>&1` and `&` are operators. POSIX bracket expressions are expanded and an
  unmatched pattern is passed literally.
- The whole line is syntax-validated before any interpolation or dispatch,
  including segments that `&&`/`||` would skip. Malformed syntax returns status
  2 with `nift: syntax error near '<token>'`; nothing runs and no interpolation
  is evaluated. A trailing `;` is valid.

## Wiring

`src/CLI.cpp` shell functions now:

- build the snapshot and call the classifier before execution;
- remove the legacy call-then-fallback branch and the bare-token
  `shell_builtins` fallback;
- execute a simple classified target exactly once;
- keep chain/pipeline/redirection/background parsing, but classify each
  segment’s first word before any part of the chain runs and validate the whole
  line before any interpolation or execution;
- reject a Nift/user/shell target used as a pipeline or redirection head with a
  clear status-2 diagnostic rather than silently externalising it (pipelines
  remain external-only in this slice);
- advertise shell-state commands, `print`, `pwd`, parser-visible user names and
  PATH executables in command position; Nift built-ins outside the allow-list
  are offered only in call form (`open(`, `file(`, `exists(`, ...). The general
  `nift complete` list and argument/file completion keep Nift expression names.

The completion lists were also kept out of the generated
`src/rt/legacy_names.cpp` extraction so this slice does **not** modify
`src/rt/**`.

## Changed existing expectation

`tests/v44_execution_shell_smoke.sh`: the command-not-found text is now the
required `nift: <name>: command not found` (previously
`command not found: <name>`), with status 127. This is an explicit D-042 P1-b
change. No other existing shell expectation changed.

## Regression suite and negative control

- `tests/shell_dispatch_unit.cpp` — 86 pure-classifier checks: precedence,
  `command`, explicit paths, env assignments, chains, pipes, redirections,
  background, quoted vs unquoted connectors/pipes, segment indices, C3 syntax
  validation, Nift-line detection and Windows path spellings.
- `tests/shell_dispatch.py` — pty/piped shell regressions:
  - `rm -i` prompt respects `n`;
  - missing/partial `rm` failures, including exactly one `rm a d1` side effect;
  - `rm [ab].txt` bracket glob;
  - `cp -r`, `mv -n`, `cat -n`, `ls -l`;
  - pipeline and stdin/stdout/stderr redirections;
  - `false &&` / `false ||`;
  - user-function side effect exactly once with no PATH fallback;
  - `mkdir`/`touch` command form reaching PATH stubs, never the native built-in;
  - function-over-PATH precedence, `command` escape, `print`, `pwd`;
  - quoted/escaped brackets, `*`, `&&`, `;`, and mixed quoted/unquoted glob;
  - explicit paths; `$[expr]` evaluated once; failed interpolation prevents
    dispatch; skipped `&&`/`||`/pipeline interpolation; left-to-right order;
    interpolation changing `PATH` cannot reclassify; `cd`; env assignment;
  - C3 malformed connectors/pipes/background/redirections, whole-line
    validation of skipped segments, no side effects, no interpolation, valid
    trailing `;` and quoted operators as words.
- Negative controls: four test-only binaries, M1 (old call-then-fallback),
  M2 (quote/escape provenance discarded), M3 (eager interpolation) and M4
  (syntax validation skipped). Normal builds contain none of their code. The
  normal suite runs against each with `--mutant-kind`; detection is reported
  PASS only when the expected failures for that mutant are observed.
  `make test-cp19-s1-dispatch` builds and runs the unit test, the normal suite,
  the completion suite and all four mutants.

## Gates run

| Gate | Result |
|---|---|
| `make test-cp19-s1-dispatch` (unit 86 checks + normal + completion + M1/M2/M3/M4) | PASS |
| `python3 tools/rt-vm/legacy_names.py --check` | PASS (208 names, unchanged) |
| `make test-v44-execution-shell` | PASS |
| `make test-v44-shell-restricted` | PASS |
| `v43_cp85_repl_multiline_smoke.sh`, `v43_cp88_repl_lifetime_smoke.sh` | PASS |
| `make test-process-hardening` | PASS |
| `make test-test-integrity` | PASS (442 files, 0 findings) |
| `make test-rt-lint` | PASS |
| `make warnings-check` | PASS (zero first-party warnings) |
| `make test-rt-vm` | PASS |
| `make test-runtime2-rt-diff` | PASS (819 entries, 0 differing) |
| ASan/UBSan classifier unit test | PASS |
| MinGW classifier/mutant build | **NOT RUN this round** (no local MinGW; infrastructure provisioning prohibited; see N3) |

Historical note: the earlier slice compiled the classifier and its unit test
under MinGW on temporary Linode `108765003` (`g6-dedicated-4`, Ubuntu 24.04,
`ap-southeast`; created 2026-10-21T10:25:41Z, deleted and API-verified absent
2026-10-21T10:36:15Z; approximately US$0.02). That build predates C1/C2/N1/N2.
**This correction round did not provision infrastructure** (the handover
prohibits it) and no local MinGW toolchain is installed, so the three mutant
CLI builds were not MinGW-compiled. Certification CI must compile the
classifier unit and all three mutants and run the existing Windows shell tests
on a Windows runner. `tests/shell_dispatch.py` and
`tests/cp19_completion.py` are POSIX-only (pty/coreutils) and exit 77
elsewhere.

## Correction round (C1, C2, C3, N1, N2, N3, N4)

### C1 — quote/escape provenance

`shell_tokens` was replaced by `shell_words`, which records each word's text,
a per-character quoted/escaped mask, and whether the word is an unquoted
operator. `CommandDispatch::classify_line` accepts the operator flags, so only
unquoted connectors, pipes and redirections split. `shell_glob_expand_word`
expands only unquoted `*?[` and escapes quoted metacharacters for `glob(3)`;
an unmatched pattern is the original text. Quoted/escaped operators remain
ordinary argument text.

All review cases match bash: `grep "[a-z]" f.txt`, `echo \[a\]`,
`echo "*"`, `echo "a" "&&" "b"`, `find ... \;`, mixed
`"pre*fix-"*.txt`, and a quoted prefix with an unquoted tail. The tests use
`/bin/echo` for exact output so the PATH `echo` stub does not prefix it.

### C2 — interpolation only in executed segments

Classification remains upfront and fixed. Interpolation now runs on the
selected segment's words immediately before dispatch; skipped segments are
never interpolated. Each interpolation runs once, left to right. Failure sets
status 2, does not dispatch, and leaves earlier segment effects intact. An
interpolation that changes `PATH`/`cwd` cannot reclassify the selected
mechanism. Regressions cover skipped `&&`, skipped `||`, skipped pipeline,
executed `||`, executed pipeline, left-to-right order, failed interpolation and
state-changing interpolation.

### N1 — line-start completion

Command position offers shell-state commands, `print`, `pwd`, user names and
PATH executables. Nift built-ins outside the allow-list are offered in call
form only; bare built-ins are suppressed even when a PATH command has the same
name. `ope<TAB>` completes to `open(`, never bare `open`; `exist<TAB>` completes
to `exists(`. The general `nift complete` API and argument/file completion keep
Nift expression names. `tests/cp19_completion.py` covers PATH, user function,
call form and no-bare-builtin.

### C3 — whole-line syntax validation

`nift::shell::validate_line_syntax` is pure and runs after classification but
before interpolation or dispatch. It rejects a leading/trailing/doubled
connector, a leading/trailing/doubled pipe, `&` anywhere except last in its
segment, and a redirection (`<`, `>`, `>>`, `2>`, `2>>`) with no following word
or with an operator as its operand. It reports `nift: syntax error near
'<token>'` and status 2. The check covers the whole line, including segments
that conditional execution would skip, so `true || echo x >` is an error and
`/bin/touch marker && echo x >` creates no marker. A trailing `;` remains
valid; quoted/escaped operators remain ordinary words; validation runs before
interpolation, so `echo $[bump()] && |` leaves the counter unchanged.

### N2 — four negative controls

Four test-only binaries are built with different CLI definitions:
`NIFT_SHELL_DISPATCH_MUTANT=1` (M1 fallback),
`NIFT_SHELL_DISPATCH_MUTANT_QUOTING=1` (M2 quotes discarded),
`NIFT_SHELL_DISPATCH_MUTANT_EAGER_INTERPOLATION=1` (M3 eager interpolation) and
`NIFT_SHELL_DISPATCH_MUTANT_SYNTAX=1` (M4 syntax validation skipped). Normal
builds contain none of their code. `--mutant-kind` runs the normal suite
against each; detection requires the expected failures for that mutant.

### N3 — Windows

No Windows/MinGW runtime evidence this round. CI must run the classifier unit
and the four mutant builds plus the existing Windows shell tests.

### N4 — whitespace

Trailing whitespace in this record was removed.

## Remaining unsupported / deferred

- Nift statements, user functions, `print` and `pwd` still execute through the
  legacy evaluator; moving them to the VM is a later CP19 requirement.
- A Nift/user/shell target as the head of a pipeline or redirection is
  explicitly refused (status 2). Pipes/redirections remain external-only in
  this slice. This avoids silently externalising a Nift name.
- Windows runtime dispatch tests were not run locally.
- No website publication; migration notes below are for the owner.

## Migration notes (website)

- `ls`, `cp`, `mv`, `rm`, `cat`, `mkdir`, `touch` and other non-allow-listed
  names in command form now run the external tool from `PATH`. For example,
  `mkdir x` runs `/bin/mkdir`; use `mkdir("x")` to call the Nift built-in.
- `print hello` and `pwd` remain Nift command-form built-ins.
- `command <name>` forces external resolution even if a shell-state command,
  user function or allow-listed built-in has that name.
- Shell invocations of Nift built-ins that are not on the allow-list no longer
  call the built-in. Use explicit call syntax for the native function.
- `$[...]` is still supported in command arguments; it is evaluated once after
  classification, and a failed evaluation prevents the command from running.
- Malformed command syntax is now rejected before execution with `nift: syntax
  error near '<token>'`, status 2. A trailing `;` is valid.
- Not-found diagnostics are now `nift: <name>: command not found`, exit 127.
- Bracket globs (`rm [ab].txt`) are expanded; an unmatched pattern is passed
  literally.
- Pipelines/redirections with a Nift/user/shell head are not supported in this
  slice and return status 2.

### Recorded compatibility limitations (not changed by CP19)

- **Empty quoted arguments are dropped.** `printf "<%s>" "" x` prints `<x>`
  (bash prints `<><x>`). This is existing Nift tokenizer behaviour, left for a
  separate compatibility decision and tests.
- **`$[...]` inside single quotes is still interpolated.** `echo '$[x]'` prints
  the value. This is released behaviour; whether single quotes should suppress
  it is a separate owner decision.
