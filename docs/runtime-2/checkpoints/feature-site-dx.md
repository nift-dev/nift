# feature-site-dx: website-generator DX fixes

Status: implemented on branch `feature-site-dx` (from `runtime-2` @`825c90a`, rebased onto `runtime-2` @`e9198eb`),
under review. Not certified: the owner runs `make certify`. Only the legacy
(shipping) engine changes. Under `src/rt/` the only edit is a comment in
`lexer.cpp`.

## Objective

Fix the defects found while dogfooding Nift as a website generator. The
sources are the site review dossier and its part 2, "silent failures". Each
fix is minimal and tested, and valid programs and sites keep their behaviour,
except for the approved changes listed per item. The owner approved items
1–6; everything else is a proposal (end of this page). User documentation is
in `docs/BUILD-DIAGNOSTICS.md`. Website work is queued in
`docs/handover/PENDING-WEBSITE.md`.

## Fixes

### 1. `nift build --auto` survives build errors

- **Before.** The first failed build ended the watcher (`src/CLI.cpp`,
  `if (result != 0) return result;`, present since the initial commit). The
  error went only to `.nift/build-auto.log` and nothing appeared on the
  terminal. A partial failure also left `.nift/.unfinished`, so later builds
  refused to run.
- **After.**
  - A failure is printed to stderr as well as to the log, under the banner
    `error: build failed; still watching (fix the error and save)`. The same
    failure is printed only once.
  - The watcher keeps running. After a failure it retries only when a
    project input changes. Change detection is a path/size/mtime fingerprint
    of the project tree that skips `.nift/`, `.git/`, `node_modules/` and the
    output directory, so a failing page is not re-rendered every 200 ms.
  - On success after a failure it prints `build succeeded again`.
  - (Originally: if the failing build created `.nift/.unfinished`, the next
    attempt ran as `build --repair`. Removed after the rebase onto
    feature-build-recovery, D-020: ordinary failures no longer need repair.)
  - Warnings from successful builds are printed to stderr as well.
  - The command is documented (`docs/BUILD-DIAGNOSTICS.md` and
    `nift commands`).
- **Compatibility.** A successful auto build is unchanged: the terminal stays
  quiet and the log has the same contents. Exit status changes only in that
  the watcher no longer exits with 1 on a build error.

### 2. Unterminated string or array literal, and `$[`, in scripts

- **Before.** `s := "abc`, or `a := [1, 2`, in `nift file.f`, `-e` or `-c`
  printed nothing after it and exited 0. The statement scanner and the
  template `$[` scanner both ran to EOF, so the strict check at
  `ParserTemplate.cpp:881` was skipped. An unterminated `$[x` statement
  behaved the same way.
- **After.**
  - The translator (`ParserScript.cpp`) fails a non-`@`, non-`$` statement
    whose scan ends at EOF inside a quote or an open `[`:
    - `unterminated string literal opened at L:C (no closing " on that line)`.
      Opener selection: a quote that is closed only on a later line is
      reported first, because that is the usual cause, as in the dossier's
      `r1a`. Otherwise the last open quote is reported.
    - `unterminated array literal or index: '[' opened at L:C has no closing
      ']'`, reported at the outermost unclosed `[`.
  - Under `strict_script_mode_`, the template parser fails an unterminated
    `$[` with `unterminated '$[' opened at L:C has no closing ']'`.
  - Exit status is 1. Nothing runs, because translation precedes execution.
  - Unclosed `(` and `{` already failed, and keep their existing diagnostic.
- **Templates (measured).** An unterminated `$[` in an HTML page publishes
  `$[` literally and the rest of the page renders normally. Pages are not
  broken, so this is unchanged. It is covered by item 6's warning: an
  identifier path followed by markup or end of line (`<p>$[title</p>`) gives
  an `unknown-value` warning, and an error under `--strict`.
- **Comment.** The comment on CP10's rt lexer `string_token` claimed "legacy
  reports it only at run time". It is corrected.
- **Compatibility.** Only programs that were already broken are affected.
  Their rest-of-program was silently dropped. Two existing errors change
  wording:
  - `x := 1 // don't` (inline `//` comments are not supported in scripts) now
    reports the unterminated `'` instead of producing no output.
  - `echo don't` (command style) now reports the unterminated quote instead
    of `@__nift_cmd has no matching ')'`.

### 3. Caught-error locations

- **Before.** An error raised directly in a `try` body reported line 1 of the
  body substring, giving `1:10` or `1:1`. An error in a `catch` body, such as
  `print` arity on line 7, was reported at 1:1. The cause was that
  `ParserTemplate.cpp` re-parsed both bodies without their `SourceView`.
- **After.** Both bodies pass `source.substr(...).view`. The dossier's repros
  now give `5:10`, `11:8`, `13:12` and `7:5`. The audit found one other
  view-less `parse(substr…)`: the lazily parsed ternary branch in `$[c ? a :
  b]`. It now passes the branch's slice of the key's view. The remaining
  view-less `parse()` calls parse whole files, so their identity views are
  correct.
- **Scope.** The fix covers every error raised in a try or catch body,
  caught or not, because the body is now parsed with its own source mapping:
  - fatal errors such as division by zero, an undefined callable, an index
    out of range, a binding type error, or `throw` of a non-Error;
  - an unknown value in a catch body;
  - a rethrow keeps its original origin.
- **Evidence.** The CP13 diagnostic corpus
  (`nift-cp13/tests/runtime2/diagnostics`, read-only) was run against this
  binary.
  - Legacy goldens: 77 pass, 4 site-dx EXPECTED-CHANGE, 8 differences.
  - The v4.12 binary passes all 89 cases against the same goldens.
  - Seven of the differences are exactly this fix, and each now gives the
    accurate position the corpus records as its `r2` intent:

    | Case | Before | After |
    | --- | --- | --- |
    | `throw_non_error` | 1:1 | 1:7 |
    | `try_catch_io_direct` | 1:10, 1:1 | 5:10, 10:12 |
    | `try_error_in_catch` | 1:21 | 5:9 |
    | `try_uncatchable_binding_type` | 1:1 | 3:3 |
    | `try_uncatchable_div_zero` | 1:1 | 2:9 |
    | `try_uncatchable_index` | 1:12 | 3:8 |
    | `try_uncatchable_undefined_callable` | 1:10 | 4:3 |

  - The eighth is `tpl_unknown_directive`, which now carries item 5's
    warning.
  - CP13 should regenerate those legacy goldens, or reclassify the cases as
    site-dx, when this checkpoint lands.
- **Compatibility.** Locations only. Output is unchanged, and the try/catch
  bodies are not re-normalised, so whitespace is unchanged.

### 4. `run()` of a missing executable

- **Before (POSIX).** `{"exit_code":127,"stdout":"","stderr":"","launched":true}`.
  `launched` was set after `fork`, not after `exec`. Windows already gave
  `launched:false` plus a `CreateProcessW failed … (error 2)` string and
  `exit_code:-1`.
- **After.**
  - POSIX: the parent creates a close-on-exec pipe per stage
    (`nift_process_pipe`). The child writes `{kind, errno}` with one
    async-signal-safe `write` before `_exit`. EOF means the exec succeeded.
  - `run("x")` gives `{"exit_code":127,…,"launched":false,"error":"executable
    not found: x"}`. EACCES gives 126 and `permission denied: x`. Other errno
    values give `cannot execute x: <strerror>`.
  - Exit codes are unchanged (127/126, f541071's convention).
    `ProcessResult.exec_failed` marks this case.
  - Pipelines: `launched` means every stage exec'd, and the first failing
    stage gives `error`.
  - Windows, reviewed by reading only: `ERROR_FILE_NOT_FOUND` and
    `ERROR_PATH_NOT_FOUND` give `executable not found: x` with exit 127;
    `ERROR_ACCESS_DENIED` gives `permission denied: x` with 126. Other
    failures keep the detailed message. A failed stage now makes `launched`
    false even when an earlier pipeline stage had started.
  - `cmd(...).run()` now carries `error` like `run()`.
  - `launched` is documented.
- **Kept.** Command-style statements (`git status` on a script line) keep
  their POSIX behaviour: a missing program has exit 127 and the script
  continues. They skip the hard error on `exec_failed`. Windows launch
  failures remain hard errors there. Making this consistent is proposal P5.
- **Consequences.**
  - The interactive shell's non-job fallback path now prints the error for
    a missing pipeline stage.
  - `git_run`'s error text for a missing `git` is now
    `executable not found: git`, where it used to be empty.
- **Assertion changed.** `tests/v44_execution_shell_smoke.sh` expected
  `launched:true` for a non-executable file (exit 126). It now expects
  `launched:false` plus `"error":"permission denied: <path>"`. This is an
  approved behaviour change, noted in the commit.
  `tests/process_contract.cpp` adds an assertion on both platforms: missing
  gives `!launched`, 127 and the message. Windows CI will verify the Windows
  half.

### 5. Template warnings

Warnings appear on stderr after the progress line, the way build errors do,
in job order. A warning in a shared template is printed once per build. Each
warning is one grep-friendly line,
`warning: <file>:<line>:<col>: <message> [<kind>]`, followed by an excerpt and
a caret. They never change output or exit status. They are collected only for
project page renders, never in scripts, `@script` blocks or standalone hosts.
They live on the parser in a shared sink, so paginated pages contribute too.

- `unknown-directive`:
  - `@name(` within one edit (OSA distance) of a call directive, or equal
    apart from case. Names shorter than 4 characters match only by case.
  - a bare `@name` close to `@content` or `@paginate`.
  - `@f(` where `f` is a function defined with `@fn`, with the hint "call it
    as `$[f(...)]`".
- Excluded, and tested as not warning: `@` preceded by a word character, `.`,
  `-`, `@`, `/`, `+`, `%`, `&`, `=` or `\`; a name followed by `-`, `/`, `.`
  or `:`; text inside `<pre>` and HTML comments; exact directive names
  without parentheses ("use @input"). The tested legitimate list:
  - addresses and handles: `me@example.com`, `a.b@c.org`, `@nick`, `1.0@beta`;
  - CSS: `@media (…)`, `@media(…)`, `@import url()`, `@font-face`,
    `@supports`, `@keyframes`, `@charset`, `@layer`, `@container`, `@page`,
    `@counter-style`;
  - npm scopes: `@types/node`, `@babel/core`, `@angular/core`, `"@scope/pkg"`;
  - decorators: `@app.route(`, `@pytest.fixture`, `@Override`, `@property`,
    `@staticmethod`, `@dataclass`;
  - in `<pre>`: `@Component({…})`, `@Injectable()`, `@Input()` and even
    `@inptu(`.
- Known false positive, documented: an Angular `@Input()` outside `<pre>`.
  Escape it as `\@Input()` or use `--no-warn=unknown-directive`.
- `path-outside-output`: an `@path`/`@pathto`/`@pathtofile` target that is
  not a tracked name and is outside `root/output-dir`. The warning names the
  page, line:col, argument and resolved link, for example
  `@path('content/faq.html') resolves to '../content/faq.html', outside the
  output directory public/ …`.
- **Silencing.** `--no-warn=<kind>` (repeatable; `all`) and the config key
  `"no-warn": [...]`, which is validated. Per occurrence: `\@` / `\$[`.
- **Machine-readable form.** Nift has no machine-readable build-diagnostic
  format (`--json` exists only for `packages` and `eval`), so none was
  invented. The single `file:line:col: message [kind]` line is the stable
  form.

### 6. Strict `$[...]`, stage 1

- **(a) Warning.** When an unknown `$[...]` falls through every legacy lookup
  and is published literally, there is now an `unknown-value` warning, but
  only if the trimmed content is an identifier path
  (`root(.member)*`, segments may contain `-`). It suggests the closest
  binding, metadata, JSON root, function or pagination name, using OSA
  distance ≤1 for names of 4 characters or fewer and ≤2 otherwise.
  Examples: `$[titel]` suggests `$[title]`, and `$[paginaet.total]` suggests
  `$[paginate.total]`.
- Both template paths are covered: the main scanner and CP06's loop-emission
  fast path.
- Output and exit status are unchanged. The regression suite's
  `META_UNKNOWN=$[not-real]` still passes (as a warning).
- **(b) Strict.** `nift build --strict` (any mode, including `--auto`) or
  `"strict-values": true` makes those cases, plus an unterminated `$[name`, a
  build error with file:line:col and a caret.
- **(c) Defaults.** `nift init` does not write `strict-values`. Default
  output is unchanged.
- **(d) Documentation.** The passthrough rule and the `\$[` escape are in
  `docs/BUILD-DIAGNOSTICS.md`. The pages nift.dev needs are queued in
  `PENDING-WEBSITE.md`: templating "Escaping literal Nift syntax",
  expressions, configuration and commands.

**Decision: which passthrough text warns.** Only identifier paths warn. That
choice is what keeps the dossier's legitimate patterns quiet:

- `$[0,1]$` and `$[a,b]$` (KaTeX intervals), `$[ x + 1 ]` and `$[$x+1]`
  (shell arithmetic), and `$[a b]` never warn, and stay literal even under
  `--strict`.
- `$[TBD]`, and Perl's `$[` followed by a name, are genuinely ambiguous, so
  they warn. They can be silenced per occurrence with `\$[TBD]`, per build
  with `--no-warn=unknown-value`, or per project with the config key.
- There is no per-page switch. `\$[` already works per occurrence, and the
  identifier-path rule removes the noisy cases (maths, shell), so a page-level
  mechanism did not seem worth a new syntax. It is noted as proposal P4.
- The `$[1+2]` → `3` hazard is unchanged. It is documented, and `\$[` is the
  remedy.

### Also: YAML flow list error (trivial, done)

`tags: [a, b]` in front matter is parsed as the string `"[a, b]"`, and schema
validation then failed with `schema post field 'tags': expected array`, with
no location. The error now reads `front matter line 4: schema post field
'tags': expected array (YAML flow lists such as [a, b] are not supported in
front matter; write a block list with one '- item' per line)`. Parsing flow
lists would change values that are strings today, so it is proposal P6.
`project.files[].extension` and pagination pages missing from `project.files`
are proposals P7 and P8: both change values that existing templates can
observe.

## Tests

- `tests/site_dx_smoke.sh` runs as `make test-site-dx`. It is added to
  `make test` and to the certification guard's REQUIRED list. It has 112
  checks (78 before review round 1), covering:
  - every dossier repro, ported;
  - multi-line and nested try/catch, a catch-body arity error, a function
    call, a try nested in `if`, and an `@if` contrast;
  - `run()` for a missing program, a non-executable file, a launched program
    that exits 127, a pipeline with a missing stage, and the unchanged
    command style;
  - the full warning matrix, including the "must not warn" list, output
    identity with `--no-warn=all`, per-kind and config silencing, invalid
    kinds and config values;
  - `--strict` and `"strict-values"` (location, caret, escapes and
    non-identifier text untouched), new projects not strict;
  - pagination-template warnings, and a shared-template warning printed
    once;
  - the YAML line number;
  - `--auto`: error printed while the watcher stays alive and does not
    repeat; recovery; recovery from its own partial failure via repair.
- `tests/process_contract.cpp` has the missing-executable contract on both
  platforms. `tests/v44_execution_shell_smoke.sh` has the updated 126
  expectation.

Regression runs are recorded at the end of this page.

## Review round 1 (independent review and owner refinements)

The independent review found no output change in any valid program or site:
the cloudflare-docs build (8,803 pages) was byte-identical. It also found zero
false positives for unterminated-literal detection over 2,721 `.f` files.
Fixes:

- **D1: watcher loop.**
  - **Cause.** With `"output-dir": "public/"`,
    `(root/"public/").lexically_normal()` keeps the trailing separator, so the
    output directory was never skipped. A hook writing under the project then
    retriggered the failed build forever.
  - **Fix: output-directory normalisation.** The output directory is
    normalised with `weakly_canonical`, without the trailing separator, and
    the project is walked from its canonical root. `public`, `public/`, an
    absolute path and a symlink to the directory are therefore the same
    directory. An output directory outside the project is simply not walked.
    No containment restriction was added.
  - **Fix: retry policy.** After a failure the fingerprint is taken after the
    build, so the build's own writes never retrigger. A failure caused by a
    change gets exactly one settling retry.
  - **Fix: banner deduplication.** The failure banner is deduplicated on its
    diagnostics (error and warning lines), not on the whole captured output.
  - **Tests.** For each of `public`, `public/`, an absolute path, an output
    directory outside the project and a symlinked one, a pre-build hook
    writes into the output directory and into the project while a page fails.
    In every case the hook-run count stays flat, the banner is printed once,
    and the watcher recovers.
- **D2: `@path` overhead.**
  - **Cause.** The check called `path_within`, which runs `weakly_canonical`
    twice per `@path`, even when the warning was disabled.
  - **Fix.** The check now tests in order: enabled kind, then tracked name,
    then a purely lexical comparison against the trailing-separator-free
    output root. It is a heuristic (symlinks are not resolved), and it does
    no filesystem work at all when disabled.
  - **One tracked-name lookup.** The `@path` handler now does one
    tracked-name lookup and reuses it for the containment check, the warning
    and the requirement. It used to do two, plus a third in the check.
  - **Callgrind** (siteB-1k, 1,008 pages, `build --all` after a warm
    build, total Ir; same host and session):

    | Binary | Ir | vs pre-site-dx |
    | --- | --- | --- |
    | pre-site-dx (`825c90a`) | 2,980,262,656 | — |
    | first site-dx version, warnings on | 3,201,932,774 | +7.4 % (D2) |
    | this version, warnings on (default) | 3,010,377,854 | +1.0 % |
    | this version, `--no-warn=path-outside-output` | 2,921,705,978 | −2.0 % |
    | this version, `--no-warn=all` | 2,921,599,955 | −2.0 % |

    Disabled, the check costs nothing. The build is cheaper than the
    baseline because of the saved lookup. Enabled, the remaining +3 % over
    "off" is the lexical normalisation of non-tracked `@path` targets (asset
    links).
- **R1: strict as a gate, and re-reporting.**
  - **Recording.** Page metadata records `"site-unknown-values"` (literal
    unknown values in checked contexts, counted whether or not the warning is
    enabled) and `"site-warnings"`. Both are written only when non-empty, so
    other pages' metadata is byte-identical.
  - **Strict.** With strict values on, through `--strict` or the config key,
    a page with a non-zero count gets the rebuild reason "strict-values is on
    and the page published unknown $[...] values", and that rebuild fails.
    Turning strict off needs no rebuild.
  - **Status.** `nift status` accepts `--strict` / `--no-warn=`, and lists
    recorded warnings on stderr.
  - **Builds.** Builds print one summary line for unchanged pages with
    recorded warnings, also when nothing was rebuilt.
  - **Test.** Strict on, off, on, off with no source edits, by CLI and by
    config: build, status and exit status agree at every step.
- **R3/R4: contexts and false positives** (the owner's refinement replaces
  the earlier "ignore uppercase-first" proposal).
  - **Non-Nift contexts.** No site diagnostic (directive or value warning,
    or strict error) is reported in `<pre>`, `<code>`, `<script>` (including
    JSON-LD), `<style>`, HTML comments, Markdown fenced code, or for a
    quote-enclosed name (`"@context"`). `<pre>` and comments use the
    scanner's existing depth counters. Inline `<code>` gets a new
    warning-only counter. `<script>`, `<style>` and fences are detected
    lazily, only when a candidate is found.
  - **Case-only matches in plain text** still warn, worded "Nift directives
    are lowercase; did you mean @content?". `@Content()` in plain text is a
    control that warns.
  - **Bare names** match only by case, substitution or transposition, so
    `@contents` and `@inputs` are no longer reported.
  - **Maths rule.** An unresolved `$[name]` immediately followed by `$` is
    left literal, with no warning and no strict error. A resolved name keeps
    its substitution (`$[title]$`).
- **FP corpus** (reviewer's `fp/`, plus a `@Content()` plain-text control).
  Warnings per category, before → after:

  | File (category) | Before | After | Remaining |
  | --- | --- | --- | --- |
  | `angular.html` (decorators) | 2 | 1 | `@Input()` in plain prose; the `<code>` one is gone |
  | `handles.html` (social handles) | 3 | 2 | `@Content` and `@Paginate` as capitalised words; `@contents` is gone |
  | `jsdoc.html` (decorators in `<pre>`/`<code>`) | 1 | 0 | |
  | `jsonld.html` | 2 | 0 | |
  | `vue.html` | 1 | 0 | |
  | `nest_java.html` (annotations) | 4 | 4 | `@Path("/users")`, `@Min(1)`, `@Max(10)`, `@Filter(name=…)` in plain prose: case-only matches, which warn by the owner's rule |
  | `python.html` | 1 | 1 | `@patch("…")` in plain prose, one edit from `@path(` |
  | `latex.html` (maths) | 18 | 4 | `$[t]_+`, `$[x]_p`, `$[n]_q` (not followed by `$`) and `$[redacted].` |
  | `shell.html` | 2 | 2 | `$[i]`, `$[count]`: legacy bash arithmetic in prose, genuinely ambiguous |
  | **Total false positives** | **34** | **14** | |
  | `typos_tp.html` (true positives) | 6/6 | 6/6 | |
  | `control_case.html` (`@Content()`) | — | 1/1 | |

  All 14 remaining are in plain template text and silenceable with `\@`,
  `\$[` or `--no-warn`. (Round 2: no further maths-suffix exemption; the
  owner prefers explicit `\$[`.)
- **R5: watcher repair workaround.** It was isolated in round 1 and then
  removed after the rebase onto `runtime-2` @`e9198eb` (feature-build-recovery,
  D-020), because ordinary page errors no longer leave a marker that needs
  `--repair`. The watcher only ever runs ordinary incremental builds.
- **R6: `run()`.**
  - **Missing interpreter.** A missing `#!` interpreter (ENOENT from execve
    of an existing file) gives `interpreter not found: /x (from ./script)`
    with 127.
  - **Child setup failures.** chdir, stdin, stdout or stderr open failures
    and `dup2` failures are reported through the error pipe as
    `launched:false` with an `error` such as "cannot change directory to …",
    exit 126 (unchanged).
  - **Pipelines** keep the conventional `exit_code` (last stage) and gain
    `stages: [{launched, exit_code[, error]}]`. The cases distinguished are:
    all stages succeed; an early stage exits non-zero while the last
    succeeds; a stage fails to launch; a stage launches and exits non-zero.
    Single-command results are unchanged (no `stages` key).
  - **Windows** fills `stages` the same way (reviewed by reading).
  - **Tests.** `tests/process_failure.cpp` now also requires the structured
    path to report an injected dup2 failure as `launched:false`.

## Review round 2 (verification blockers B1–B3, N1–N3, owner refinements)

- **B1: strict skipped pages without a record.**
  - **Fix.** `"site-unknown-values"` is now written on every successful
    build, 0 included.
  - **Strict.** Under strict, a missing, non-numeric or non-integral count
    means "not verified". The page is rebuilt with the reason "…has no
    unknown-value record (needs verification)", and fails if it has unknown
    values. `status --strict` lists it and never says "all up to date".
  - **Without strict**, a missing field changes nothing.
  - **Tests.** Field removed; field `"zero"`; optionally metadata written by
    an old binary (`NIFT_OLD_BIN`).
  - **Cost.** Every page's metadata gains one line.
- **B2: strict now follows substitution exactly** (owner rule: evidence-exact,
  never heuristic).
  - **What can fail.** Only an interpolation that the substitution code
    attempted and could not resolve can fail strict: the unresolved
    identifier-path `$[...]` that reaches the passthrough branch. This
    applies in every context, including `<pre>`, `<script>`, comments and
    `<code>`, because Nift substitutes there too.
  - **The one exemption** is the exact maths rule: an unresolved name
    immediately followed by `$`.
  - **Never fatal under any flag:** directive near-misses, unknown
    directives and every similarity-based warning. The unterminated `$[name`
    warning is now warning-only too: the scanner never attempts that
    substitution.
  - **Error message.** The strict error names the exact escape:
    "strict-values is on: write `\$[nosuch]` to publish it literally".
  - **Contexts quiet warnings only.** They are recognised per source
    document in one forward pass, cached per document, with a binary search
    per candidate. Only properly closed contexts count. Nothing leaks across
    files, because each `@input` file is its own document.
  - **Tests.** Every case in p2 (`pre`, `scr`, `com`, `code`, plus `inc`)
    and p4 (`b`, `c`, `d`) fails under strict. In default mode the warnings
    stay suppressed in properly closed contexts, while `b`, `c` and `d` now
    warn (no suppression by badly closed markup or across files). Typos and
    an unterminated `$[` pass `--strict`.
- **B3: `--auto` lost a fix saved during the follow-up retry.** The policy
  is `BuildAutoRetryPolicy` in `src/CLI.cpp`:
  - **Healthy:** no snapshots.
  - **First failure** is retried once.
  - **Later attempts** are bracketed by snapshots, and files changed during
    an attempt are hashed. A repeat of the previous attempt's own write with
    identical content is ignored. Any other change triggers another attempt,
    so no edit is lost.
  - **Hook rewriting files every build.** If the same files change with new
    content on two consecutive attempts, this is reported once as "a build
    hook modifies watched files on every build" and the watcher waits for an
    edit.
  - **Tests.** Six deterministic scenarios run with a slow per-page hook
    that runs after the edited page was read: a fix during the failing
    rebuild, a fix during the follow-up retry (w1), rapid saves, atomic
    rename saves during a build and between builds, a hook rewriting the
    same content, and a hook writing new content every build. They all have
    bounded waits, alongside the D1 output-directory loop tests.
- **N1: deterministic pipeline test.** The first stage is `true`, which
  writes nothing and cannot hit SIGPIPE; the exact per-stage statuses are
  still asserted. Repeated runs: see below.
- **N2: pipeline top-level semantics.** For pipelines, `launched` and `error`
  keep their pre-feature meaning: the stages were started, and only
  parent-side setup errors are reported. The per-stage `stages` array carries
  each launch failure. On Windows, single commands only get the new wording
  and the 127/126 codes. Single commands keep item 4's `launched:false`
  together with `error`.
- **N3: quadratic context scan.** Replaced by the per-document spans above.
  The guard builds pages with 5,000 and 20,000 `$[…]` candidates and requires
  roughly linear time.
- **Owner refinements.**
  - **No P14.** Nift prefers explicit literal marking (`\$[`) to
    syntax-suffix heuristics, so no further exemptions are added.
  - **Heavy jobs** go through `heavy.sh`.

### Round 2 results and rebase onto build recovery

- **Rebase.** Squashed and rebased onto `runtime-2` @`e9198eb`. The
  pre-rebase history is kept on the local branch
  `feature-site-dx-prerebase-6d8dfa6`.
  - **Conflicts** were in `ProjectInfo.cpp` (`build_one_attempt` /
    `write_page_info` / the `build_many` no-op path), the certification guard
    and `progress.md`.
  - **Site warnings with retries.** A page's site warnings now come from its
    latest render attempt, so a retried attempt does not duplicate them.
  - **Workaround removed.** `BuildAutoRepairWorkaround` and its documentation
    are gone. The watcher only runs ordinary incremental builds.
  - **Strict with recovery.** After a failed strict build, recovery records
    the page in `.nift/failed.json`. The next ordinary build rebuilds it
    once (status: "previous build of this page failed"), and status is then
    up to date. The toggle test asserts exactly this.
- **Blocking repros** (copies of `sitedx-verify/p2`, `p4`):
  - **Default mode.** No warning inside the closed contexts in p2, except
    `inc.txt` (its own file). p4 `b`, `c` and `d` each warn on `$[titel]`.
  - **Strict.** p2 `pre`, `scr`, `com`, `code` and `inc`, and p4 `b`, `c` and
    `d`, all fail (exit 1). The clean pages pass.
  - **w1.** Covered by watcher scenario 2.
  - **B1 with real old-binary metadata** (v4.12 / `825c90a`): `status` up to
    date; `status --strict` lists all 3 pages as "needs verification";
    `build --strict` fails the dirty page; the clean pages now record 0.
- **FP corpus under `--strict`.** `nift build --all --strict` fails only
  pages with real attempted, unresolved interpolations. Each count is the
  page's recorded `site-unknown-values`:
  - **latex:** 4 — `$[t]_+`, `$[x]_p`, `$[n]_q`, `$[redacted].`;
  - **shell:** 2 — bash `$[i]`, `$[count]`;
  - **typos_tp:** 1 — `$[titel]`. Its unterminated `$[title` is warning-only.

  Every one is a real substitution attempt: Nift would substitute it if the
  name were defined. Directive-only pages (angular, nest_java, python,
  handles, jsonld, vue, control `@Content()`) pass strict. Default-mode
  warnings per file are unchanged from round 1: 14 false positives, typos
  6/6, control 1/1.
- **N1.** The exact three-stage pipeline result
  (`true | missing | cat`) matched 20/20 consecutive runs. The full smoke
  test passed 3 consecutive times on the rebased binary: 159 checks each.
- **Perf** (siteB-1k, `build --all`, total Ir, under `heavy.sh`). Output
  `public/` is byte-identical to `825c90a` (1,011 files; 0 warnings):

  | Binary | Ir | vs `825c90a` | vs `e9198eb` |
  | --- | --- | --- | --- |
  | `825c90a` (pre-site-dx) | 2,980,783,635 | — | |
  | `e9198eb` (runtime-2 with build recovery) | 2,984,655,699 | +0.13 % | — |
  | this branch, warnings on | 3,004,567,187 | +0.80 % | +0.67 % |
  | this branch, `--no-warn=path-outside-output` | 2,918,153,687 | −2.10 % | −2.23 % |
  | this branch, `--no-warn=all` | 2,916,623,811 | −2.15 % | −2.28 % |

- **N3.** 5,000 → 20,000 candidates: 90 ms → 355 ms, which is linear.
- **Runs on the rebased branch** (`make -j1`; heavy jobs via `heavy.sh`):
  - **Pass:** `test-site-dx` (159 checks), `test-build-recovery`,
    `test-process-hardening`, `test-runtime2-rt-diff`, `test-contracts`,
    `test-template-variables`, `test-template-optional`, `test-diagnostics`,
    `test-v44-execution-shell`, `test-pagination`, `test-control-flow`,
    `test-runtime2-certification-guard`, `test-test-integrity`.
  - **Checks:** `legacy_names --check` up to date (208 names).
  - **Regression suite** (`runtime-2` @`523d1df`): 93/94 modules pass. The
    failure is `unreadable_source`, which fails as root.

## Proposals (not implemented)

- **P1. `.nift/.unfinished` lock-out softening.** Done separately by
  feature-build-recovery (D-020), on which this branch is now rebased.
- **P2. Strict by default for new projects.** Have `nift init` write
  `"strict-values": true`, and make it the default in the next major version
  with `false` as the opt-out. The legacy suite line `META_UNKNOWN=$[not-real]`
  would then move under the opt-out.
- **P3. `@raw{...}` block** for large code samples that should never be
  parsed.
- **P4. Per-page warning control,** for example a front matter
  `nift-no-warn: [unknown-value]`, if `\$[` turns out to be too fine-grained
  for maths-heavy pages.
- **P5. Command-style missing executable.** Make it a hard error on POSIX
  too, as on Windows and as the code comment intends, or print
  `nift: x: command not found` and continue, as Bash does. Either way the
  behaviour should be the same on all platforms.
- **P6. YAML flow sequences** (`[a, b]`) in front matter, parsed as arrays.
- **P7. `project.files[].extension`** reports the content extension, not the
  output extension. Add `output_extension` rather than change the existing
  field.
- **P8. Pagination secondary pages in `project.files`.**
- **P9. Sitemap and feed helpers.**
- **P10. `@path('/about')` message.** Suggest dropping the leading slash
  (dossier A5).
- **P11. SIGINT/SIGTERM in `--auto`.** Killing the watcher during a build can
  leave a stale marker, as before. Install a handler that finishes or
  abandons the build cleanly.
- **P13. Script lines that are template text are silently dropped**
  (investigated at the coordinator's request; CP13 case
  `syn_dollar_line_silent`). For example, `$o := nosuch_function(1)`,
  `$ ls -la` (a pasted shell prompt), `$x`, or `@inptu("x")` on a script line.
  - **Mechanism.** The statement translator (`ParserScript.cpp`) copies a
    statement starting with `@` or `$` verbatim, so that template statements
    such as `$[...]` and `@directive(...)` work. The template parser then
    treats `$o …` (no `[`) or an unknown `@name(` as literal text.
    `run_script` discards rendered text, so the line does nothing and the
    script exits 0.
  - **Why not fixed here.** It is a defect, but fixing it has a
    compatibility impact: scripts that run today with such lines would start
    failing (exit 0 → 1).
  - **Proposal.** In strict script mode, make non-whitespace literal text at
    statement level an error, for example "statement is not valid script
    syntax: `$o := …` (did you mean `o := …` or `$[...]`?)". Introduce it
    first as a warning if the owner prefers a staged change, as for item 6.
- **P12. Machine-readable build diagnostics**, for example a `--diagnostics=json`
  output for errors and warnings, if agents need more than the grep-friendly
  line.

## Regression runs (this branch, Linux, as root, `make -j2`)

- **Pass.** `test-site-dx` (three consecutive runs), `test-contracts`,
  `test-conformance`, `test-runtime2-dynamic-reads`, `test-runtime2-fs-freedom`,
  `test-runtime2-rt-diff`, `test-rt-vm` (CP11 VM parity; see below),
  `test-process-hardening` (with the extended `process_contract`),
  `test-v44-execution-shell`, `test-pagination`, `test-pagination-ordering`,
  `test-template-variables`, `test-template-optional`, `test-control-flow`,
  `test-diagnostics`, `test-parser-hardening`, `test-commands`, `test-content`,
  `test-comments`, `test-metadata-safety`, `test-path-safety`,
  `test-path-alias`, `test-config-validation`, `test-cross-feature`,
  `test-markup-json-directives`, `test-requirements`,
  `test-incremental-modified-immediate`, `test-runtime2-certification-guard`,
  `test-test-integrity`.
- **Host-only failures, identical on the v4.12/`825c90a` binary.**
  `tests/v46_b4_cp4a_filesystem.sh` (`io.remove_failed` in a read-only
  directory) and `tests/v43_cp112_filevalue_wall_smoke.sh` (via
  `test-v43-language`). Both fail because root can write a `chmod 555`
  directory.
- **rt-diff / VM.** Legacy behaviour changes do not affect the rt-diff
  corpus, which compares the binary with itself: 0 differences. They also do
  not affect the CP11 VM parity suites. The first `test-rt-vm` run failed
  only in `tools/rt-vm/legacy_names.py --check`, because the new directive
  word lists, written as brace lists of string literals, were picked up as
  "names legacy dispatches specially". Writing them as one space-separated
  literal keeps `src/rt/legacy_names.cpp` unchanged (208 names) and the VM
  coverage unchanged. No expectations were updated.
- **Regression suite.** `nift-regression-suite` `runtime-2` @`523d1df`,
  `./run-contract.sh`: 93/94 modules pass. The failure is
  `unreadable_source`, the known runs-as-root failure. The baseline binary
  also fails it, plus `v44_executable_script`, which needs `nift` on PATH.
  The historical module, including `META_UNKNOWN=$[not-real]` "unknown
  metadata preserved", passes; it now produces an `unknown-value` warning on
  stderr. **No suite assertion needed to change**, so there is no patch.
- **CP13 diagnostic corpus.** See item 3. Re-run after review round 1, with
  CP13's updated classification: 77 pass, 11 site-dx EXPECTED-CHANGE, and 1
  FAIL, `tpl_unknown_directive`. That case is classified parity but now
  carries the intended `@inptu` warning, so CP13 should reclassify it.
- **Review round 1 re-runs** (binary after the D2 change, `make -j1`, heavy
  jobs under the shared lock):
  - **Pass:** `test-site-dx` (112 checks), `test-process-hardening`,
    `test-conformance`, `test-rt-vm`, `test-runtime2-rt-diff`,
    `test-contracts`, `test-pagination`, `test-pagination-ordering`,
    `test-template-variables`, `test-template-optional`, `test-control-flow`,
    `test-diagnostics`, `test-runtime2-dynamic-reads`,
    `test-runtime2-fs-freedom`, `test-v44-execution-shell`,
    `test-parser-hardening`, `test-commands`, `test-content`,
    `test-comments`, `test-metadata-safety`, `test-path-safety`,
    `test-path-alias`, `test-config-validation`, `test-cross-feature`,
    `test-markup-json-directives`, `test-requirements`,
    `test-incremental-modified-immediate`,
    `test-runtime2-certification-guard`, `test-test-integrity`.
  - **Checks:** `legacy_names --check` up to date (208 names); rt-lint clean.
  - **Regression suite:** 93/94 modules pass. The failure is
    `unreadable_source`, which fails as root.
