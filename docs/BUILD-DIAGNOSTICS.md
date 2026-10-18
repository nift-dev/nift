# Watch builds, build warnings and literal `$[...]` text

This page documents the site-building behaviour added or clarified in the
`feature-site-dx` checkpoint. The public website needs matching changes; see the
queue in [handover/PENDING-WEBSITE.md](handover/PENDING-WEBSITE.md).

## `nift build --auto`

```sh
nift build --auto            # add --strict / --no-warn=<kind> as for any build
```

`build --auto` polls the project every 200 ms and runs an ordinary incremental
build when something changed. Press `q` to stop it when stdin is a terminal. In
non-interactive use (a service, a background job), stop the process.

- **Output.** The latest build output is kept in `.nift/build-auto.log`, without
  colour codes. The file is rewritten only when the output changes. Successful
  builds keep the terminal quiet.
- **Errors.** A failed build is printed to stderr, as well as to the log, and
  the watcher **keeps running**. The same set of diagnostics is printed only
  once. When the build succeeds again, `build succeeded again` is printed.
- **When a failed build is retried.** While builds succeed, every poll runs
  an incremental build, which picks up any edit. After a failure, the watcher
  takes snapshots (path, size, mtime) of every file under the project root,
  ignoring `.nift/`, `.git/`, `node_modules/` and the output directory:
  - **First failure.** The first failure after a success is retried once,
    because what changed while it ran is unknown.
  - **Later attempts.** Every later attempt is bracketed by snapshots. A file
    that changed while the attempt ran is hashed. If its content equals what
    the previous attempt left there, it is the build's own repeated write and
    is ignored. Any other change causes another attempt: a save, an atomic
    rename, a new or deleted file, or several saves where only the last is
    the fix. **No edit is lost.**
  - **A hook that rewrites the same content** settles after one extra
    attempt.
  - **A hook that writes new content on every build.** If the same files
    change with new content on two consecutive attempts, the watcher prints
    `warning: a build hook modifies watched files on every build (…); waiting
    for an edit before retrying` once, and waits for an edit instead of
    looping.
  - **While idle after a failure**, any difference from the state the last
    attempt left triggers the next attempt.
  - No snapshots are taken while builds succeed.
  - The output directory is recognised however it is spelled: `public`,
    `public/`, an absolute path, or a symlink to the directory.
  - An output directory outside the project is simply not walked. There is no
    containment requirement.
  - Directory symlinks inside the project are not followed.
- **Warnings.** Build warnings (below) are printed to stderr when the page that
  produces them is rebuilt. Warnings recorded earlier for unchanged pages are
  summarised in one line.
- **Failed pages and `--repair`.** With build recovery (D-020), an ordinary
  page error never needs `nift build --repair`. A failed page keeps its
  last good output and is listed in `.nift/failed.json`. The next build after
  your fix rebuilds it, and `--auto` does this on its own. The watcher never
  runs `--repair`.
- **Stopping the watcher.** Press `q` in a terminal, or send SIGINT
  (Ctrl-C) or SIGTERM. A build in progress finishes first, so no
  interrupted-build state is left. The watcher then prints `build --auto
  stopped` and exits 0.
- **Limitation.** While a build is failing, the watcher does not watch inputs
  outside the project root (for example, files read through `--fs-root`). Save
  any project file to retry.

## Build warnings

Warnings never change the generated output or the exit status. They are
produced when a page is rendered, so a build prints them for the pages it
rebuilds. Each page's warnings are also recorded in its build metadata, so they
are not lost:

- a build prints one summary line for unchanged pages with recorded warnings,
  for example `warning: 3 earlier warnings in 2 unchanged pages: about, blog
  (nift status lists them)`;
- `nift status` lists every recorded warning on stderr (stdout keeps its
  format);
- both respect the current `--no-warn` / `"no-warn"` settings.

Each warning has one line that is easy to grep, followed by the source line and
a caret:

```text
warning: content/about.html:2:1: unknown directive @inptu published literally (did you mean @input?) [unknown-directive]
    @inptu("templates/partials/cta.html")
    ^~~~~~
```

| Kind | When |
| --- | --- |
| `unknown-directive` | An `@name(` that is one edit from a directive (`@inptu(`), or the same as one apart from case (`@Input(`, `@Content()`, worded "Nift directives are lowercase; did you mean @input?"). A bare `@name` that differs from `@content` or `@paginate` by case, one substitution or one transposition (`@contnet`, `@Content`); insertions such as `@contents` are not reported. `@f(...)` where `f` is a function defined with `@fn`; call it as `$[f(...)]`. |
| `path-outside-output` | An `@path`/`@pathto`/`@pathtofile` target that is not a tracked name and lies outside the output directory, for example `@path('content/faq.html')`, which gives `../content/faq.html`. The link works locally but 404s once the output directory is deployed. The test is purely lexical, so symlinks are not resolved; it is a heuristic. When the kind is disabled, the check does no work at all. |
| `unknown-value` | A `$[name]` or `$[name.member]` whose root is not a binding, metadata, JSON or pagination value. It is published literally (see below), with "did you mean" when a close name exists. Also an unterminated `$[name` before markup or the end of a line. |

**Non-Nift contexts quiet warnings only.** No *warning* is reported in
recognised non-Nift contexts, where code samples, scripts, styles, JSON-LD and
commented-out markup live:

- `<pre>`, `<code>`, `<script>` (including `application/ld+json`) and
  `<style>` elements;
- HTML comments;
- Markdown fenced code (a line starting with ```` ``` ```` or `~~~`);
- quote-enclosed names, which are JSON keys and values such as `"@context"`
  and `"@type"`.

How these contexts are recognised:

- **Per file.** Contexts are recognised within each source file. A context
  opened in an `@input` file never affects the page that includes it, and
  the reverse.
- **Closed contexts only.** A context counts only if it is properly closed
  in the same file. An unclosed `<code>`, `<!--` or `<script>`, or a final
  unpaired fence, suppresses nothing.
- **One pass per file.** Recognition is a single forward pass over each file,
  so the cost is linear in its size.
- **No effect on substitution.** Nift still substitutes directives and values
  in these contexts exactly as before.
- **Strict values ignore contexts.** A strict-values error does not depend on
  context (see below).

Elsewhere, `unknown-directive` also stays quiet for these ordinary `@word`
uses:

- e-mail addresses and handles inside words (`me@example.com`);
- CSS at-rules: `@media`, `@import`, `@font-face`, `@supports`, `@keyframes`,
  `@page`, `@layer` and others;
- npm scopes (`@types/node`);
- decorators with member access (`@app.route(`);
- decorators that are not close to a directive (`@Component(`, `@Override`);
- prose mentions of real directives without parentheses ("use @input").

A case-only match in plain text, such as Angular's `@Input()` outside a code
element, still warns. Put code samples in `<code>` or `<pre>`, or escape them
with `\@`.

### Silencing warnings

- **Per build:** `nift build --no-warn=<kind>`, repeatable. Use `--no-warn=all`
  to silence every kind.
- **Per project:** the `"no-warn"` key in `.nift/config.json`, for example
  `"no-warn": ["path-outside-output"]` or `["all"]`. Unknown kinds are rejected.
- **Per occurrence:** escape the text, as in `\@inptu(` or `\$[TBD]`. The output
  is the text without the backslash.

Warnings are reported by `nift build` only. Scripts (`nift file.f`, `-e`, `-c`)
have their own, stricter rules.

## Literal `$[...]` text in templates (passthrough rule)

In a template or content page, Nift tries to resolve `$[...]` in this order:

1. as an expression;
2. as a pagination value;
3. as built-in metadata;
4. as JSON.

If all of these fail **because the name is unknown**, Nift publishes the text
literally, exactly as written. This is a long-standing compatibility rule. It
is why `$[0,1]$` (a maths interval), `$[ x + 1 ]` (shell arithmetic in a code
sample) and `$[TBD]` pass through unchanged. It has two hazards:

- A typo such as `$[titel]` is published silently. This is what the
  `unknown-value` warning is for.
- Some literal text happens to be valid Nift and is evaluated. For example,
  `$[1+2]` becomes `3`.

**The reliable way to write literal `$[` is `\$[`**, for example `\$[title]`
or `echo \$[1+2]`. The backslash is removed from the output. `\@` and `\#` work
the same way.

The `unknown-value` warning fires only when the text inside `$[...]` is an
identifier path such as `titel`, `site.titel` or `build-date`. Text that cannot
be a value reference never warns, for example `$[0,1]`, `$[ x + 1 ]`,
`$[$x+1]` or `$[a b]`.

**Maths rule: the one exemption.** An *unresolved* `$[name]` immediately
followed by `$` (TeX or KaTeX maths such as `$[n]$` or `$$[x]$$`) is left as
literal text. It is not warned about and does not fail strict values.

- **Resolution comes first.** A name that resolves keeps its current
  substitution, so `$[title]$` still renders the title followed by `$`. No
  output changes.
- **No other spelling is exempt.** `$[x]_p` or bash `$[i]` are real
  substitution attempts: if `x` or `i` were defined, Nift would substitute
  them.
- **Escape.** Write `\$[` to publish such text literally. Nift prefers this
  explicit marking to syntax heuristics.

### Strict values (opt-in)

`nift build --strict`, or `"strict-values": true` in `.nift/config.json`, makes
strict values a build error. Strict values follow Nift's own substitution:

- **What fails.** A build fails exactly when the template engine attempted a
  `$[name]` or `$[name.member]` substitution and could not resolve the root
  name, in any context (`<pre>`, `<script>`, comments and so on included,
  because Nift substitutes there too).
- **The one exemption** is the maths rule above.
- **Never fatal under any flag:** directive near-misses and unknown
  directives (`@inptu(`, `@Content`, `@patch(`), any other similarity-based
  warning, and an unterminated `$[name` (the scanner never attempts that
  substitution).
- **Passthrough unchanged.** Text that is not an identifier path (`$[0,1]`,
  `$[ x + 1 ]`) keeps the passthrough rule.

The error names the exact escape:

```text
error: while building /
  content/index.html:4:11
  unknown value $[nosuch]; strict-values is on: write \$[nosuch] to publish it literally
    <p id="u">$[nosuch]</p>
              ^~~~~~~~~
```

`nift init` does not enable strict values, and the default output is
unchanged.

**Strict is a reliable gate, without full rebuilds.**

- **Recording.** Every successful build records in the page's metadata how
  many unknown values it published literally (`"site-unknown-values"`, 0
  included).
- **Unverified pages.** Metadata without a valid count (written by an older
  Nift, edited, or non-numeric) is "not verified". Under strict the page is
  rebuilt, and fails if it has unknown values. `nift status --strict` lists
  it as needing verification ("…has no unknown-value record (needs
  verification)"), never as up to date. Without strict, a missing count
  changes nothing.
- **Turning strict on.** When strict values are on, through the flag or the
  config key, every page with a non-zero count needs rebuilding. That rebuild
  fails. `nift status --strict` (or `nift status` with the config key) lists
  those pages with the reason "strict-values is on and the page published
  unknown $[...] values".
- **Pages with no unknown values** render identically either way, so they are
  not rebuilt.
- **Turning strict off.** A strict build that succeeded recorded no unknown
  values, so nothing is rebuilt. A strict build that failed kept the page's
  last good output. Build recovery also lists the page as failed
  (`.nift/failed.json`), so the next ordinary build rebuilds it once and
  succeeds; `status` shows "previous build of this page failed" until then.

So with no source edits, toggling strict on, off, on, off gives
fail/ok/fail/ok, and build, status and exit status agree at every step.

## Scripts: unterminated literals

In scripts (`nift file.f`, `nift -e`, `nift -c`), a string literal, `[` or `$[`
that is never closed is an error. Exit status is 1 and nothing runs:

```text
error: post.f:2:6: unterminated string literal opened at 2:6 (no closing " on that line)
error: post.f:2:6: unterminated array literal or index: '[' opened at 2:6 has no closing ']'
error: <command-line>:1:13: unterminated '$[' opened at 1:13 has no closing ']'
```

Until this release, everything after the unclosed literal was dropped
silently, with exit status 0. A string that legitimately spans lines is still
valid.

## `run()` and the `launched` field

`run(...)` and `cmd(...).run()` return
`{exit_code, stdout, stderr, launched[, error][, stages]}`.

- `launched` is `true` when the program, or every stage of a pipeline, started
  executing. Its own exit status is then in `exit_code`, even when that status
  is 126 or 127.
- `launched` is `false` when the program could not be started. `error` explains
  why, for example `"executable not found: rsync"` or `"permission denied:
  ./build.sh"`. `exit_code` follows the shell convention: 127 when the program
  was not found, and 126 when it was found but could not be executed.
- A script whose `#!` interpreter (or ELF loader) is missing gives `"interpreter
  not found: /usr/bin/python9 (from ./tool)"` and 127.
- Setup failures in the child before `exec` give `launched:false` and 126.
  Examples are `cmd(...).cwd(dir)` naming a missing directory, or a
  `stdin`/`stdout`/`stderr` file that cannot be opened. The error reads, for
  example, "cannot change directory to /x: No such file or directory" or
  "cannot open stdin in.txt: …".
- **Pipelines** (`cmd(a).pipe(cmd(b))…run()`) keep the conventional
  `exit_code`: the last stage's status. A pipeline result also has `stages`,
  one `{launched, exit_code[, error]}` per stage, so an early stage that
  failed is never hidden:
  - `launched:false` with `error`: the stage could not be started;
  - `launched:true` with a non-zero `exit_code`: it ran and failed;
  - `"error":"not started"`: it was never started after an earlier setup
    failure.

  For pipelines, the top-level `launched` and `error` keep their meaning from
  before this feature:
  - `launched` means the stages were started;
  - `error` is set only for a setup failure in the parent (pipe or fork);
  - the per-stage launch failures are in `stages`.

  For example, if `b` is missing in `a | b | c`, the result has
  `launched:true` and no top-level `error`. Its `exit_code` is `c`'s status
  (possibly 0), and `stages[1]` is
  `{"launched":false,"exit_code":127,"error":"executable not found: b"}`.
  Always check `stages` for pipelines. Single-command results have no
  `stages` key, and report a launch failure in `launched:false` and `error`.

Linux and macOS report a failed `exec` through a close-on-exec pipe. Windows
reports `CreateProcessW` failures. Both give the same `launched`, `exit_code`
and `error` for a missing or non-executable program.

A command-style statement in a script (`rsync -a src/ dst/` on a line of its
own) is unchanged on Linux and macOS. A missing program has exit status 127
and the script continues. On Windows a program that cannot be launched is
still an error.
