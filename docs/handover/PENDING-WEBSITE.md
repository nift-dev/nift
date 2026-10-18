# Pending Nift website changes

## Purpose

This is the internal queue for Nift implementation changes that require matching
updates in the separate public website repository before the supporting Nift
version is released. It prevents release-coupled documentation work from being
forgotten without treating the website as frozen during normal development.

Do not put ordinary independent website improvements here. They may be developed,
built and published through the website's normal workflow whenever appropriate.
Add an item here only when publishing it too early would describe behavior that
the current public Nift release does not yet support, or when omitting it from the
next release would leave the website inaccurate.

## Working procedure

1. When a Nift change creates website work, add an item in the same development
   checkpoint or commit series. Record the behavior, intended website sections,
   earliest supporting Nift version, and any timing constraint.
2. Keep the item current if implementation semantics or the target release
   changes. This file is the queue of record; do not rely on chat context.
3. Continue unrelated website work normally. A pending release-coupled item does
   not block other website commits or deployments.
4. During release preparation, review every open item against the candidate.
   Implement all items targeted at that release in the website source, build the
   website with the exact candidate Nift executable, and verify the generated
   site and examples.
5. Record the website source and generated-site commits in the release report.
   Remove completed queue items in the Nift release-preparation commit. If an
   item is deliberately deferred, update its target version and record that
   decision rather than silently carrying stale wording forward.
6. Do not tag the Nift release while an item targeted at that version remains
   unresolved.

Use this compact shape for new entries:

```markdown
### Short change name

- Status: pending
- Earliest release: X.Y.Z
- Website scope: pages/examples/downloads that must change
- Required update: concise description of the public wording or example
- Timing: why it must not be published early, if applicable
```

## Open items

### Native SHA-256 built-ins (`sha256`, `sha256_file`)

- Status: pending (implemented on branch `feature-sha256`, off `runtime-2`)
- Earliest release: the first release that includes the `feature-sha256`
  checkpoint (not v4.12.0)
- Website scope: scripting reference (filesystem and bytes/value built-ins),
  the built-in index, and any release-manifest or artifact-verification example
- Required update: document `sha256(value)` (string/bytes contents, lowercase
  hex, no normalisation, other types are errors) and `sha256_file(path)`
  (streamed raw file bytes, `open()`-style errors, dependency tracking in
  builds, user definitions with the same name take precedence), following
  `docs/HASHING-API.md`
- Timing: describes built-ins no released Nift has

### Site DX: watch builds, build warnings, strict values, `launched`

- Status: pending (implemented on branch `feature-site-dx`, off `runtime-2`)
- Earliest release: the first release that includes the `feature-site-dx`
  checkpoint (not v4.12.0)
- Website scope: `docs/commands` (build modes), `docs/configuration`,
  `docs/templating` ("Escaping literal Nift syntax"), `docs/expressions`,
  `docs/scripting-run-shell`, `docs/runtime-errors` (optional)
- Required update, following `docs/BUILD-DIAGNOSTICS.md`:
  - commands: `build --auto` no longer exits on a build error. The error is
    printed to stderr as well as `.nift/build-auto.log`, the watcher keeps
    running, and it retries on the next project change, repairing its own
    partial failure automatically. Replace "build --auto keeps the terminal
    quiet" with "quiet while builds succeed". Add `--strict` and
    `--no-warn=<kind>` (orthogonal to the build mode).
  - configuration: `"strict-values": true|false` (default false; `init` does
    not set it) and `"no-warn": ["unknown-directive" | "path-outside-output"
    | "unknown-value" | "all"]`.
  - templating/escaping: say that an unknown `$[name]` is published literally
    (the passthrough rule), that this is what makes `$[0,1]$` and shell
    `$[ x + 1 ]` survive, and that it is not reliable (`$[1+2]` becomes `3`).
    `\$[` is the way to write literal text. Unknown identifier-path values
    warn; `--strict` makes them errors.
  - expressions: one paragraph on the passthrough rule and a link to the
    escaping section.
  - templating: list the three build warning kinds and how to silence them.
  - scripting-run-shell: define `launched` (true when every stage started).
    A missing or non-executable program gives `launched:false`, an `error`
    ("executable not found: x" / "permission denied: x") and exit code
    127/126, on every platform. A missing `#!` interpreter reads
    "interpreter not found: X (from ./script)"; child setup failures (cwd,
    stdin/stdout/stderr files) give `launched:false` + error, 126.
    `cmd(...).run()` also carries `error`, and pipelines add per-stage
    `stages: [{launched, exit_code[, error]}]` while `exit_code` stays the
    last stage's status.
  - templating: warnings are silent in `<pre>`, `<code>`, `<script>`,
    `<style>`, HTML comments, fenced code and quoted JSON keys; the maths
    rule (`$[n]$` left literal when unresolved); recorded warnings are
    summarised for unchanged pages and listed by `nift status`;
    `--strict` rebuilds (and fails) exactly the pages with unknown values.
  - scripting: an unterminated string, `[` or `$[` is an error, reported at
    the opening position.
- Timing: describes behaviour no released Nift has

### Local preview server (`nift serve`)

- Status: pending (implemented on branch `feature-serve`, off `runtime-2`)
- Earliest release: the first release that includes the `feature-serve`
  checkpoint (not v4.12.0)
- Website scope: `docs/commands` (new `serve` entry), and the getting-started
  or quick-start page where `nift build` is shown
- Required update, following the README section "Previewing locally with
  `nift serve`" and `docs/runtime-2/checkpoints/feature-serve.md`:
  - commands: `nift serve [--port N] [--host H] [--dir PATH]` serves the
    project's output directory (or any `--dir`) at
    `http://127.0.0.1:8000/` by default. It is equivalent to
    `python3 -m http.server PORT --bind HOST --directory DIR`. It stops with
    Ctrl+C and lists its options with `--help`.
  - show `nift build` then `nift serve`. It does not build, watch or reload:
    run `nift build` (or `nift build --auto` in another terminal) to update
    what it serves.
  - requirements: Python 3.8 or newer on PATH (`python3`/`python`; on Windows
    the `py` launcher or a `python.exe`). Nift never installs it.
  - say that it is for local development only, not production; deploy the
    output directory with a real web server or static host.
  - errors: a missing output directory suggests `nift build`; an occupied
    port is reported before launch (best effort).
- Timing: describes a command no released Nift has

None other for the current candidate. All five items targeted at v4.6.0 (native time
functions, native secure random bytes, execution output separation, relative
import module ownership, explicit module/package resource paths) were completed
in the website source during v4.6 release preparation and verified against the
candidate; see the v4.6.0 release-verification evidence.

## Completed items

Move durable historical context to the relevant release record when useful; do
not let this file become a second changelog.

The template-less tracked-entry documentation was completed in the website
source/generated checkpoints for Nift 4.0.1. It covers direct parsed content,
ordinary `@content` templates, historical empty-string compatibility, dependency
replacement, and removal of identity CSS/JavaScript template guidance.

The v4.0.3 language/pagination/installer documentation has continued to track the candidate. It covers exactly-one rendered `@content`, logical conditions, lazy ternary rendering, `@join`, UTF-8-safe `@substr`, pure `$[expression]` arithmetic, pagination configuration/runtime metadata and relative/absolute `@pathtopage`, plus the immutable composable collection/aggregation surface through `@reduce`. The website also documents the functional-programming flavour and its deliberate no-mutation boundary, the installer endpoint, the successful Store-built strict-Snap validation (including project-local `.nift/` state), and updated reliability evidence.
