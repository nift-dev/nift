# NIFT v4.11.0 — RELEASE COMPLETE

GitHub release and agent-owned publication/verification are complete. **Blockers: NONE.** Chocolatey moderation and maintainer-only Snap promotion remain separate downstream states; no approval or stable availability for those new store versions is claimed.

## Immutable source and notes

- Candidate/tag target: `c8d9c266518d115e4f289c5c6a2b94ddd7ee9968`.
- Annotated tag object: `aca0cc8bfe5b0b906b300d6f92ad1ef9bb23b9e2`; established message `Nift v4.11.0`.
- Pre-tag fetch confirmed HEAD == origin/main, ahead/behind 0/0, tracked clean, version consistency PASS, reviewed notes without placeholders. No candidate changes preceded tagging.
- [Release](https://github.com/nift-dev/nift/releases/tag/v4.11.0): published, draft NO, prerelease NO. [Canonical release workflow #38049463374](https://github.com/nift-dev/nift/actions/runs/38049463374): PASS, including public installer lanes.
- Canonical/public release-note SHA256: `6e23387174acf897f898d7db9f29af1ff0edcdc23797d9ba8a25420b22794304`. API body bytes exactly equal reviewed `release-notes-4.11.0.md`.
- C ABI remains 1.3. Tag will not be moved by this post-release evidence commit.

## Actual public assets

Downloaded from the public GitHub release and hashed afresh. Exactly four archives plus SHA256SUMS; no extras or omissions. SHA256SUMS matches every actual archive.

| Platform | Public archive SHA256 | Architecture / contents |
| --- | --- | --- |
| linux-x86_64 | `445083b2e932ec775a1ca0076e88a7593aacf5843df36a7cd30ec0e8c21b0c7b` | PASS |
| macos-arm64 | `958d3a1695b64e72936f77d37579c817823f24c3490b50dcde059ca2884749f4` | PASS |
| macos-x86_64 | `56ce0da59bd9bca933bcd4e6bf69ee165e9edd9abcc0c0e7fd8222fe0cfcf146` | PASS |
| windows-x86_64 | `e513bc9bf8a126a51b5f5329ca4641eabc6013327b223ea5c278d7fed573a3d3` | PASS |

ELF x86_64, Mach-O arm64, Mach-O x86_64 and PE x86_64 verified. Each root contains only nift/nift.exe, README.md and LICENSE. README/LICENSE equal tagged bytes (Windows CRLF normalized); no debug, scratch, historical evidence, drafts or receipts shipped. Windows imports contain only KERNEL32 and Universal CRT API DLLs, with no libgcc/libstdc++/libwinpthread/libffi runtime dependency.

**Rehearsal-byte comparison: DIFFER for all four archives.** All Unix executables and every README/LICENSE are identical to rehearsal payloads. Windows binaries have identical size and differ in exactly five bytes confined to PE build timestamp/checksum fields. Archive build metadata is not a deterministic-byte contract. These differences were inspected, not treated as public checksum failures. Detailed independently measured receipts are in `publication/public-artifact-inspection.json` and `publication/windows-reproducibility.json`.

## Public artifact and installer certification

[Native public distribution #38049878898](https://github.com/nift-dev/nift/actions/runs/38049878898): PASS on Linux x86_64, macOS arm64, macOS x86_64 and Windows x86_64, downloading the actual public assets. This is independent of rehearsal execution evidence.

Downloaded Linux public executable: --version/version, about, commands, unknown/help diagnostics, init/build/status PASS. NRS SHA `6da99128a5ea053551fe952a4994a390a38d0258`: **94/94 PASS**. PRS SHA `1da43659da269c96af21aea7234799d2ad56b2df`: **12/12 PASS**. Raw terminal receipts are retained under publication.

Live installer https://nift.dev/install is byte-identical to tagged packaging/install.sh; shell syntax PASS. SHA256 `a98bdf72c2c5bd3aed0181f16e0361220b15cf02a2b96d183d98702be6d90708`. Isolated local public install pinned to 4.11.0 reports Nift v4.11.0; init/build/status PASS, installed binary exactly equals the published Linux executable. Canonical public installer jobs also PASS on Linux and both macOS architectures.

## Chocolatey

[Submission workflow #38049985758](https://github.com/nift-dev/nift/actions/runs/38049985758): PASS, including actual Publish step. Package `nift.4.11.0.nupkg`, SHA256 `3e358ae5014d62d24f4c000f0dfe257f2f7236d64e4e5e90cc9d2d40ff3c7fa4`. Nuspec version 4.11.0 and public Windows URL/checksum match the actual release. Feed observation: **Submitted / Pending; IsApproved=false**. Approval and public installability are not claimed. No duplicate resubmission was performed.

## Snap: read-only observation, no promotion

Connected Store edge metadata at observation:

| Architecture | Version | Revision |
| --- | --- | --- |
| amd64 | 4.11.0 | 1762 |
| arm64 | 4.11.0 | 1764 |
| armhf | 4.11.0 | 1763 |
| ppc64el | 4.11.0 | 1766 |
| riscv64 | 4.10.0 | 1761 |
| s390x | 4.11.0 | 1765 |

All five required architectures expose 4.11.0 edge metadata; riscv64 remains 4.10.0 and is best effort/non-blocking. Stable still reports 4.10.0. No channel mutation/promotion was invoked. Metadata is build-availability evidence, not a certification of exact build commit or non-native executable bytes. Maintainer inspection of the connected build page/provenance precedes manual promotion.

## Website publication

- Authoritative source stage: `63ebd5617f9adca37de007a14941c171cf15dcf6`.
- Generated public/main: `1f5950619891fb9322e5ca1c3aaae881b2b0d5ef`.
- [Pages deployment #38050102226](https://github.com/nift-dev/nift-dev.github.io/actions/runs/38050102226): PASS.
- Generated main was committed and pushed before authoritative stage with matching gitlink; both checkouts clean and synchronized.
- Released public binary built 109 files; immediate incremental invocation reported all 109 up to date.
- Generated affected pages inspected; transformation/canonical display and 19,841 local reference checks PASS; agent discovery/sitemap checks PASS.
- Live homepage, installing, incremental builds, build scripts, scripting/run/shell and commands returned200 and exactly matched generated bytes. Homepage metadata and release link identify 4.11.0.
- Incremental docs describe per-consumer history, conservative old-state migration, stable external inputs and explicit FileValue dependencies. Process docs describe child-only overrides, fail-closed redirects, Unicode/capture behavior and platform limits. Trusted-code boundary explicit; commands unchanged; no arbitrary external ABA or hostile sandbox claim.
- Frozen benchmark claims/performance labels and Jsonic++ unchanged.

## Preservation and closeout

Original 38 untracked historical files remain unchanged and uncommitted; explicit fresh-checkout archive staging and exact payload checks prove exclusion. Full readiness/rehearsal local receipts remain outside tagged release inputs. This post-release report and curated publication receipts are committed after tagging; canonical notes and tag remain immutable. No new feature, performance, official benchmark or architecture work occurred. Stop for next direction.
