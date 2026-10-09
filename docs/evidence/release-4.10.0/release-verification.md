# NIFT v4.10.0 — GITHUB RELEASE FINAL

- Tag: **v4.10.0**, immutable annotated object `00acc981185ab3946a5a4f88e583ea17b5c5834f`.
- Target: `76dbe2661fe7d221827db63959bf4a392bae2ba6`.
- [Publishing workflow 37987877884](https://github.com/nift-dev/nift/actions/runs/37987877884): **PASS**, including all three public installer lanes and the `publish` job.
- [GitHub release](https://github.com/nift-dev/nift/releases/tag/v4.10.0): **published**, draft **NO**, prerelease **NO**.
- Release body equals committed canonical reviewed notes byte-for-byte; notes SHA-256
  `003d7fffe8f9af1468dc1e5f6396046b3a9c4258f45c32e0a20ae6d7b6eac785`.
- Release blockers: **NONE**. Version **4.10.0**, C ABI **1.3**.

## Actual public assets

Exactly **five** public assets: the four archives below and `SHA256SUMS`.
All four unique manifest digests match actual downloaded public archives
(`sha256sum -c SHA256SUMS` = OK for all four).

| Archive | Architecture | SHA-256 |
|---|---|---|
| `nift-4.10.0-linux-x86_64.tar.gz` | x86_64 | `c4bf59a9a7c95cf8c4a1fea8f6dd176712e6f6fec2d574090407eac6958a74ef` |
| `nift-4.10.0-macos-arm64.tar.gz` | arm64 | `36d93d1a4cb3d065bd345b1ae8a7c5a3cf1008a0814b6dd1c500de3225ec7d41` |
| `nift-4.10.0-macos-x86_64.tar.gz` | x86_64 | `b433a03850d087503f2a1980d8a5023adb7e6b04b9ef872b0f1473accb143da9` |
| `nift-4.10.0-windows-x86_64.zip` | x86_64 | `56e5a1d40edf70d7166b5bc8d3087257df189ea7cb4ef08281350c5dc3ac9bfa` |

Archive inspection PASS for every published archive: exact platform root containing
the executable (`nift`/`nift.exe`), `README.md` and `LICENSE` only. ELF x86_64,
Mach-O arm64, Mach-O x86_64, PE x86_64; extracted executable reports
`Nift v4.10.0`; README/LICENSE match the tagged source bytes (Windows modulo CRLF).
No debug, evidence, certification or stale-v4.9 content shipped. Windows imports
contain only Universal CRT and KERNEL32; no libgcc/libstdc++/libwinpthread/libffi
runtime DLLs. The publishing workflow rebuilt from the immutable tag, so public
archive hashes differ from the non-publishing rehearsal (timestamps/build metadata
can differ); the actual public payload was independently checked against its
emitted manifest, architecture, version, tagged source identity and extracted
binary hash.

## Public installer

Verified canonical/live byte identity: `https://nift.dev/install` is byte-identical
to `packaging/install.sh` (SHA-256
`a98bdf72c2c5bd3aed0181f16e0361220b15cf02a2b96d183d98702be6d90708`). Isolated Linux
x86_64 install from the live installer pinned to 4.10.0 installed the published
archive; the installed binary (SHA-256
`e7cb28d6150c797acbb52a618faff3cf981281b9ea4e91a8c478062242f0c3a2`) matches the
published linux archive executable, reports `Nift v4.10.0`, and init/track/build/
render/status passed in a temporary project. Public installer smokes PASS on macOS
ARM64 (`macos-latest`) and macOS x86_64 (`macos-26-intel`) in the publishing
workflow's `installer-public-smoke` jobs against the actual public URLs and
checksums.

## Website

Release status **PASS**. Generated public main `d0f94c1850d9e27bf3bebc43d900f994ac6ae8bd`
was published before authoritative stage `8174994`. Live homepage/install/
build-scripts/build-systems/asset-pipelines all return HTTP 200 and carry the
v4.10.0 release-status wording (homepage JSON-LD `softwareVersion` 4.10.0, latest
release link, released v4.10 build-workflow pages). Source and generated
repositories are clean and synchronized; the stage gitlink matches published main.

## Chocolatey

[Workflow 37989307795](https://github.com/nift-dev/nift/actions/runs/37989307795):
**PASS**. The log confirms `nift.4.10.0.nupkg` was pushed successfully to
`https://push.chocolatey.org/`. The nupkg was built from the actual published
Windows archive checksum and the package reports version 4.10.0. **PASS**.
Submitted for moderation; public approval/installability is not claimed.

## Certification and boundary

Canonical non-publishing rehearsal 37985929505 PASS. The candidate is
source-equivalent to the ten-wall-certified runtime `2eaec7d`: the only diff is
`docs/evidence/release-4.10.0/*` (release notes/audit), which no gated workflow
watches. Final local native, maintained and aggregate binding, GCC/Clang + native
wrapper warning, sanitizer/lifetime, NRS 94/94, PRS 12/12 and retained v4.10
performance gates PASS. macOS Clang and Windows MinGW hosted first-party warning
and cross-platform coverage PASS at the source-equivalent runtime; no MSVC gate
exists or is claimed. The NRS clean-up harness fix (washer leak + temp-root
clobber) shipped as a **fixed non-runtime release-readiness issue**. Frozen
official benchmark series `20261009-v490` unchanged: not rerun, and no new
cross-language result is claimed.

No official post-v4.10 benchmark rerun, Labs update, provisioning, new feature or
performance implementation occurred during publication. Snap and Homebrew were not
published or certified in this task. Raw run/log, package, downloaded archive and
local installer evidence is retained in ignored `.build/release-4.10.0/`. Canonical
release notes remain immutable. The established next minor development version is
4.11.0; its version transition is a separate local commit after this verified
release.