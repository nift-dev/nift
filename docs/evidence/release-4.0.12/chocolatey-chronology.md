# Chocolatey v4.0.12 chronology (read-only diagnosis)

This record reconstructs the v4.0.12 Chocolatey submission timeline from
retained logs and GitHub evidence. It is read-only; no corrective action was
taken. Proven timestamps are separated from inference.

## Proven timestamps

| Event | Timestamp (UTC) | Source |
|---|---|---|
| GitHub release record created | 2026-09-10 10:42:15 | GitHub API (`release.createdAt`) |
| GitHub release published | 2026-09-10 10:44:08 | GitHub API (`release.publishedAt`) |
| Release workflow run started | 2026-09-10 10:42:21 | Actions API (`run.created_at`) |
| All release assets (incl. `nift-4.0.12-windows-x86_64.zip`, `SHA256SUMS`) created | 2026-09-10 10:44:08 | GitHub API asset `created_at` |
| Chocolatey job started | 2026-09-10 10:44:14 | Actions job log |
| Chocolatey job completed | 2026-09-10 10:44:39 | Actions job log |
| `choco pack` success | 2026-09-10 10:44:28 | chocolatey job log |
| `choco push` success | 2026-09-10 10:44:35 | chocolatey job log (`nift.4.0.12.nupkg was pushed successfully`) |
| Chocolatey automated validation failure recorded | 2026-09-10 11:16:20 | Chocolatey package page moderation log (`chocolatey-ops`: "nift has failed automated validation … We were unable to find a package") |

## Proven facts

- The release asset `nift-4.0.12-windows-x86_64.zip` was public and downloadable
  when the Chocolatey job constructed the package: the job downloaded it,
  computed its SHA-256 (`9913fba1725be227a440d98da106bbb28f2594803390787f8b088d2118b8a895`),
  extracted it, and verified the embedded `nift.exe` reported `Nift v4.0.12`
  before packing.
- The exact submitted `.nupkg` was later verified: SHA-256
  `f1709b315ad90d58ae01fbb0b83cee841f8bb273ba15fd1de87467498a67a2b2`, and the
  retained artifact is byte-identical to the one the release run uploaded and to
  the one currently served by the Chocolatey API at the exact-version URL.
- `choco push` reported success to `https://push.chocolatey.org/` at 10:44:35,
  before the release run completed (10:47:32).
- Chocolatey's automated validator recorded at 11:16:20 that it was "unable to
  find a package" and could not continue validation, leaving the version in
  "Waiting for Maintainer to take corrective action".

## Assessment

- The GitHub download URL was available before `choco push` completed, so a
  later publication race is **not credible** as the cause of the validator
  failure.
- The package was structurally valid, its embedded checksum matched the live
  asset, and a real Windows install/uninstall smoke of the exact package passed.
- The evidence points to a **Chocolatey repository/backend ingestion or
  validator retrieval failure** at or shortly after upload (the validator could
  not retrieve the package it was asked to validate), rather than a package
  defect.

## Inferred (not proven)

- The exact moment Chocolatey indexed the submission is not in retained logs;
  the push accepted at 10:44:35 and the validator failure at 11:16:20 bound the
  window.
- Whether the ingestion was accepted-then-lost, or never ingested, is not
  distinguishable from public evidence alone.

## Action

None. Chocolatey v4.0.12 remains under moderation and must not be resubmitted
without a separate decision.