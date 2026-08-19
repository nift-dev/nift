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

None currently.

## Completed items

Move durable historical context to the relevant release record when useful; do
not let this file become a second changelog.

The template-less tracked-entry documentation was completed in the website
source/generated checkpoints for Nift 4.0.1. It covers direct parsed content,
ordinary `@content` templates, historical empty-string compatibility, dependency
replacement, and removal of identity CSS/JavaScript template guidance.

The v4.0.3 language/pagination/installer documentation has continued to track the candidate. It covers exactly-one rendered `@content`, logical conditions, lazy ternary rendering, `@join`, UTF-8-safe `@substr`, pure `$[expression]` arithmetic, pagination configuration/runtime metadata and relative/absolute `@pathtopage`, plus the immutable composable collection/aggregation surface through `@reduce`. The website also documents the functional-programming flavour and its deliberate no-mutation boundary, the installer endpoint, strict-Snap experiment status, and updated reliability evidence.
