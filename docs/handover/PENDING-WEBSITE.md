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

### Native time functions

- Status: pending
- Earliest release: 4.6.0
- Website scope: scripting API reference and runtime concurrency documentation
- Required update: document `epoch()` as integer Unix milliseconds and
  `sleep(ms)` as a signed-64-bit, non-negative blocking delay on the current
  execution thread. Document `timer()` as a stopped monotonic stopwatch with
  `start()`, `elapsed()`, `pause()`, `resume()`, `stop()`, `reset()`,
  `running()`, and `paused()`, including opaque identity semantics and the v1
  prohibition on thread/future or embedding transfer.
- Timing: do not publish before a Nift release containing these functions

### Native secure random bytes

- Status: pending
- Earliest release: 4.6.0
- Website scope: scripting/bytes API and security/reproducibility guidance
- Required update: document `secure_random_bytes(n)`, its 10,000,000-byte cap,
  native-OS-only CSPRNG contract, ordinary non-erased heap storage, and that use
  in templates makes generated output nondeterministic
- Timing: do not publish before a Nift release containing this function

### Execution output separation

- Status: pending
- Earliest release: 4.6.0
- Website scope: scripting console API and all embedding/binding result references
- Required update: document `err(value)`, CLI stdout/stderr behavior, per-operation
  embedding captures on success and failure, worker inheritance and atomic writes,
  and the separation from return values, diagnostics, and rendered content
- Timing: do not publish before a Nift release containing this behavior

### Relative import module ownership

- Status: pending
- Earliest release: 4.6.0
- Website scope: scripting imports and package authoring guidance
- Required update: document that path-shaped relative imports resolve from the
  source module containing the import, including escaped package callables and
  nested/re-exported modules; missing siblings never fall through to the
  consumer root. Document canonical package-root confinement, delayed package
  read locking, and worker-local module snapshots. Keep bare package-name and
  absolute-path behavior distinct.
- Timing: do not publish before a Nift release containing this behavior

## Completed items

Move durable historical context to the relevant release record when useful; do
not let this file become a second changelog.

The template-less tracked-entry documentation was completed in the website
source/generated checkpoints for Nift 4.0.1. It covers direct parsed content,
ordinary `@content` templates, historical empty-string compatibility, dependency
replacement, and removal of identity CSS/JavaScript template guidance.

The v4.0.3 language/pagination/installer documentation has continued to track the candidate. It covers exactly-one rendered `@content`, logical conditions, lazy ternary rendering, `@join`, UTF-8-safe `@substr`, pure `$[expression]` arithmetic, pagination configuration/runtime metadata and relative/absolute `@pathtopage`, plus the immutable composable collection/aggregation surface through `@reduce`. The website also documents the functional-programming flavour and its deliberate no-mutation boundary, the installer endpoint, the successful Store-built strict-Snap validation (including project-local `.nift/` state), and updated reliability evidence.
