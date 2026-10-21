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
- `$[expr]` in command arguments is evaluated exactly once, **after**
  classification and before dispatch. If interpolation fails, the command does
  not execute; interpolation can change argument text but not the selected
  dispatch mechanism.
- Globbing is applied once to external arguments. POSIX `[ab]`, `[a-z]` and
  `[!x]` bracket expressions are expanded (`glob(3)`), and an unmatched pattern
  is passed literally.

## Wiring

`src/CLI.cpp` shell functions now:

- build the snapshot and call the classifier before execution;
- remove the legacy call-then-fallback branch and the bare-token
  `shell_builtins` fallback;
- execute a simple classified target exactly once;
- keep chain/pipeline/redirection/background parsing, but classify each
  segment’s first word before any part of the chain runs;
- reject a Nift/user/shell target used as a pipeline or redirection head with a
  clear status-2 diagnostic rather than silently externalising it (pipelines
  remain external-only in this slice);
- advertise only `print`, `pwd`, the shell-state commands, parser-visible user
  names and PATH executables in command position; the general `nift complete`
  list and argument/file completion keep Nift expression names.

The completion lists were also kept out of the generated
`src/rt/legacy_names.cpp` extraction so this slice does **not** modify
`src/rt/**`.

## Changed existing expectation

`tests/v44_execution_shell_smoke.sh`: the command-not-found text is now the
required `nift: <name>: command not found` (previously
`command not found: <name>`), with status 127. This is an explicit D-042 P1-b
change. No other existing shell expectation changed.

## Regression suite and negative control

- `tests/shell_dispatch_unit.cpp` — 62 pure-classifier checks: precedence,
  `command`, explicit paths, env assignments, chains, pipes, redirections,
  background, quoting-adjacent tokens, Nift-line detection and Windows path
  spellings.
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
  - explicit paths; `$[expr]` evaluated once; failed interpolation prevents
    dispatch; `cd`; env assignment.
- Negative control: a separate binary built with
  `-DNIFT_SHELL_DISPATCH_MUTANT=1` restores the old call-then-fallback path.
  Normal builds contain none of that code. The same suite in `--mutant` mode
  observes the old double execution (`rm a d1` retries; the user function’s
  file gets two appends and the PATH stub runs).
  `make test-cp19-s1-dispatch` builds and runs both.

## Gates run

| Gate | Result |
|---|---|
| `make test-cp19-s1-dispatch` (unit + normal + mutant) | PASS |
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
| MinGW classifier + unit test compile | PASS |

MinGW was verified on a temporary Linode `108765003`
(`g6-dedicated-4`, Ubuntu 24.04, `ap-southeast`): created
2026-10-21T10:25:41Z, deleted and API-verified absent
2026-10-21T10:36:15Z. List price US$0.108/hour; approximately US$0.02 for the
10.5-minute lifetime (at most US$0.108 if the provider rounds to an hour).
The instance used the `nift-rt2-campaign`, `purpose-cp19-s1-mingw` and
`ttl-1792592736` tags, pinned SSH host-key verification, and no credentials
were transferred to it. MinGW binary sha256:
`7c0535920562fcb16acb413ff0d0fe144d365d16302d9a05efc45cf7e69975b7`.

Windows runtime execution of the Python regression suite was not run (no
Windows host); the classifier and unit test compile under MinGW.

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
- Not-found diagnostics are now `nift: <name>: command not found`, exit 127.
- Bracket globs (`rm [ab].txt`) are expanded; an unmatched pattern is passed
  literally.
- Pipelines/redirections with a Nift/user/shell head are not supported in this
  slice and return status 2.
