# Bounded v4.11 post-release website checklist

Timing: after an explicitly authorized GitHub v4.11.0 release. No website files
or public release state were changed during preparation. User direction for this
release is checklist-only; these updates are deliberately queued after publication.

- Homepage/template metadata and latest-version data: point to the real release,
  keeping performance evidence labels and frozen benchmark claims unchanged.
- `content/docs/installing.html`: update latest published version/release link
  and describe the correctness/hardening release; check installer availability.
- `content/docs/commands.html`: verify examples and flags; no new CLI syntax is
  introduced by this release, so no invented command change is needed.
- `content/docs/incremental-builds.html` and `content/docs/build-scripts.html`:
  explain per-consumer hash/hybrid history, conservative metadata migration,
  stable-input contract and declared FileValue dependencies. No external ABA claim.
- `content/docs/scripting-run-shell.html`: child-only environment overrides,
  fail-closed redirects, Unicode/capture behavior and supported platform limits.
- Check trusted-code wording across scripting/packages/imports/hooks; controls
  are API restrictions, not hostile-repository sandboxing.
- Build the authoritative website source with the release candidate, inspect
  generated pages/examples, then follow its normal publication workflow only
  when separately authorized. Do not hand-edit generated HTML.
