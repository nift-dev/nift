# Nift distribution verification handover

## Purpose

This is a **post-release** gate. It validates the public packages and archives a
new user actually receives; it does not rebuild Nift from the repository and it
does not substitute for release-candidate testing.

The maintained workflow is:

```text
.github/workflows/distribution-verification.yml
```

The common black-box smoke harness is:

```text
scripts/distribution_smoke.py
```

The summary classification is script-backed:

```text
scripts/distribution_summary.py
```

Run the workflow manually after an intended release has been published and the
relevant package managers have had a chance to propagate it. The workflow input
`version` is **required** and has no default; it must be supplied explicitly and
is validated as a strict `X.Y.Z` release version. Do not lower the expected
version merely because a store is still serving an older package: a red channel
is useful evidence that propagation or moderation is incomplete.

## Channels

### Maintained release channels

The workflow can independently select these maintained channels:

- public GitHub release archives (all four published platform archives);
- the canonical Homebrew formula;
- the Chocolatey Community Repository package;
- Snap Store `latest/edge` (promotion readiness); and
- Snap Store `latest/stable` (the maintained public-release gate).

The GitHub-release job downloads and extracts all four published platform
archives (Linux x86-64, macOS arm64, macOS x86-64 and Windows x86-64). Homebrew
is exercised on representative macOS arm64 and Linux x86-64 runners.
Chocolatey uses a clean Windows runner. Snap edge and Snap stable each use a
clean Ubuntu runner and are verified independently.

### Externally maintained or legacy channels

The Flathub listing (`cc.nift.nsm`) is **externally maintained and legacy**. It
is owned by the Flathub repository and is not part of Nift's maintained release
pipeline. The maintained Distribution Verification workflow does **not** query,
install, test or evaluate Flathub, and no Flathub job is retained as an optional
informational check. Flathub's version state therefore never determines the
maintained-distribution release gate.

Automated or AI-agent contribution work to the Flathub repository is outside
this project's release process.

A package-manager workflow/build succeeding during publication is deliberately
not treated as distribution evidence. This gate installs from the public
channel after publication.

## User-realistic contract

Every installed launcher runs the same smoke contract:

1. `nift version` must report exactly the requested public version;
2. `nift about` must succeed;
3. `nift commands` must succeed and expose the 4.0.2+ init syntax;
4. `nift init` must initialize the default HTML starter;
5. `nift build` must produce `public/index.html`;
6. `nift init --target=vercel` must initialize the representative native-output
   target;
7. the target build must produce `.vercel/output/config.json` with Build Output
   API version 3 and `.vercel/output/static/index.html`.

This deliberately checks the released executable rather than package-manager
metadata alone. It proves that the package is installable, launches, reports the
right version, and can perform representative real work.

The package-manager jobs remove any pre-existing Nift package before installing
and attempt to uninstall it afterward. The cleanup is not itself a release
contract; it keeps hosted runners isolated and makes the install path fresh.

## Version input

The `version` input is required and has no default. Each maintained-channel job
depends on the `validate_version` job, which requires the strict `X.Y.Z` format
(`^[0-9]+\.[0-9]+\.[0-9]+$`) and fails the run on anything else. Every selected
maintained-channel job uses that same validated version.

## Snap edge versus Snap stable

Snap edge and Snap stable are separate jobs with separate inputs (`snap_edge`,
`snap_stable`):

- **Snap edge** demonstrates that the release artifact reached `latest/edge`
  and is ready for promotion.
- **Snap stable** is the maintained public-release gate.

An edge success is **never** reported as a stable success. The summary records
each channel separately and classifies the combined Snap state:

```text
stable_correct                    latest/stable serves the requested version
edge_correct_stable_pending       latest/edge serves the version; stable does not
version_mismatch                  an installed Snap reports a different version
install_runtime_failure           a Snap install or runtime step failed
not_selected                      no Snap channel was selected
```

A pre-promotion run may enable edge and disable stable. That run can be green
while explicitly reporting that stable promotion remains pending; it is not the
final stable-channel gate.

## Evidence

Each successful smoke emits normalized JSON containing:

- channel identity;
- expected and reported Nift versions;
- runner OS and architecture;
- basic-site and Vercel-target observations; and
- SHA-256 hashes of representative generated outputs.

The JSON is uploaded as a GitHub Actions artifact. The final `Distribution
summary` job records which maintained channels were selected, each job result,
and the independent Snap edge/stable classification. A failed install may not
have a per-channel smoke artifact because the Nift executable was never
available; the job log plus summary result is then the evidence.

Record a fully green run URL in `PACKAGING.md` for the release once every
maintained channel that is claimed as publicly available has passed. If a
channel is still under moderation or propagation, leave the strict version
assertion intact and rerun later.

## Interpreting failures

Classify failures before changing Nift:

```text
older version installed
    -> store propagation / moderation / formula lag / Snap stable pending

package cannot be installed
    -> distribution-channel/package problem

correct version installs but version/about/commands fails
    -> packaging/runtime defect

basic init/build fails
    -> packaged-executable or confinement/runtime defect

Vercel target smoke fails
    -> released 4.0.2 init-target regression/package runtime issue

runner/package-manager infrastructure failure
    -> CI/environment issue; rerun or fix the harness without weakening Nift's contract
```

A Snap edge success with stable still on an older version is reported as
`edge_correct_stable_pending`: the release artifact reached edge and is ready
for promotion, while the stable gate is not yet satisfied. Do not confuse that
with `stable_correct`, and do not normalize or skip a real package failure
merely to make the distribution matrix green.

## Relationship to future releases

The workflow is version-parameterized rather than hard-coded to a specific
release. For a future `X.Y.Z` release, run it with `version = X.Y.Z` only after
the public GitHub release exists. Channels can be deselected deliberately when
Nift no longer claims or maintains that distribution path; temporary moderation
delays are not a reason to delete a maintained channel from the workflow.