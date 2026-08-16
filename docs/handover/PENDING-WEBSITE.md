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

### Template-less tracked entries

- Status: pending
- Earliest release: first release after 4.0.1 containing commit `4ebbef6`
- Website scope: tracked-entry/configuration documentation, introductory examples,
  and any CSS/JavaScript setup instructions that recommend identity templates
- Required update: explain that omitting `template` makes the content file the
  fully parsed top-level Nift source; contrast this with a genuine template using
  `@content`; state that an explicitly empty `template` is invalid; remove any
  instruction to create `template.css` or `template.js` containing only
  `@content`
- Timing: do not publish this behavior on the website until a publicly available
  Nift release supports it

## Completed items

Move durable historical context to the relevant release record when useful; do
not let this file become a second changelog.
