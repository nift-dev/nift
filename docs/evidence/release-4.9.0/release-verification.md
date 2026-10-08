# NIFT v4.9.0 — GITHUB RELEASE FINAL

- Tag: **v4.9.0**, immutable annotated object `1314a4b169ea2588df8bf63a3f342639ea7b5aa7`.
- Target: `aaadeb31219251b7cc02a62fbf75360ac3e11aaf`.
- [Publishing workflow 37809541710](https://github.com/nift-dev/nift/actions/runs/37809541710): **PASS**, including all three public installer lanes.
- [GitHub release](https://github.com/nift-dev/nift/releases/tag/v4.9.0): **published**, draft **NO**, prerelease **NO**.
- Release body equals committed canonical reviewed notes byte-for-byte after newline normalization; notes SHA-256 `9b1e33535c82599ea55c647e143b56b21033182ff7903f6f405bdc2bce5c0142`.
- Release blockers: **NONE**. Version **4.9.0**, C ABI **1.3**.

## Actual public assets

Exactly **five** public assets: the four archives below and `SHA256SUMS`.
All four unique manifest digests match actual downloaded public archives.

| Archive | Architecture | SHA-256 |
|---|---|---|
| `nift-4.9.0-linux-x86_64.tar.gz` | x86_64 | `793866c850fe8f6f228aa7f699cbfcd958b9158ad866904dda3081704d2c6283` |
| `nift-4.9.0-macos-arm64.tar.gz` | arm64 | `5b617062daa5b36006be7e9d18593ebaf3376c8d4aef26da58ac88ed0945e4ec` |
| `nift-4.9.0-macos-x86_64.tar.gz` | x86_64 | `8f8e6d5ca2a60fd0444219eda545224acdfdf8bae8b6ee2e16be16b2e94ad7d9` |
| `nift-4.9.0-windows-x86_64.zip` | x86_64 | `3f7a0bd2cb2b6716d0b8cf36bc2a21a80fa189b9e5a3bf747bf11ac09e9d0283` |

Archive inspection PASS: exact platform root containing executable, README.md,
and LICENSE only; ELF x86_64, Mach-O arm64/x86_64, PE x86_64; version 4.9.0;
README/LICENSE match tagged source; no unwanted assets or evidence sidecars.
Windows imports contain no libgcc/libstdc++/libwinpthread/libffi runtime DLL.
Linux ARM64 remains certification-only.

The publishing workflow rebuilt from the immutable tag. Public archive hashes
need not equal rehearsal hashes because timestamps/build metadata can differ.
The actual public payload was independently checked against its emitted manifest,
architecture, version, tagged source identity and native extracted-archive smokes.

Public installer smokes PASS on Linux x86_64 and both macOS architectures:
actual public URLs, checksums, version, project init/build/status. Additional
isolated Linux verification passed four init/render modes and canonical
workbooks, repeat installation from public URLs, and rejection of a deliberately
invalid checksum while preserving the existing installed binary. No user-home
installation was used for the independent checks.

## Website

Release status **PASS**. Generated main `59c53ca531ef729c97e4c09ac0edfafd408e4695`
was published before authoritative stage `3f4fed8`. Pages run 37811457413 PASS.
All eight requested live homepage/install/commands/transformation/performance/
installer surfaces match deployment bytes. Source and generated repositories
are clean and synchronized. The site identifies v4.9.0 as current; experimental
v4.8-introduced workflows and historical benchmark qualifications remain accurate.

## Chocolatey

[Workflow 37812050345](https://github.com/nift-dev/nift/actions/runs/37812050345):
**PASS**. The log confirms `nift.4.9.0.nupkg` was pushed successfully.
Package SHA-256: `8c6385abb645bad27263cc0e99d5cc939ff9095f06a293c686d3324df36ba811`.
The downloaded nupkg contains version 4.9.0, final public Windows URL and SHA-256,
canonical install helper and expected VERIFICATION metadata. **PASS**.
Submitted for moderation; public approval/installability is not claimed.

## Certification and boundary

Canonical non-publishing rehearsal 37805478979 PASS. All ten normal walls PASS
at source-equivalent `a3e5919`; the changed general compiler diagnostic additionally
passed at final `aaadeb3` in run 37803547448. Final local native, maintained and
aggregate binding, GCC/Clang/native wrapper warning, sanitizer, NRS 93/93,
PRS 12/12 and retained CP49 performance gates PASS. macOS Clang and Windows
MinGW hosted first-party warning checks PASS with zero warnings; no MSVC gate
exists or is claimed. The final source change only routes the general macOS
diagnostic to Intel; required native ARM64 gates remain and passed.

Frozen official series `20261008-v480` unchanged: all 2,828 path hashes checked.
No official post-v4.9 benchmark rerun, Labs update, provisioning, new feature or
performance implementation occurred during publication. Deferred architecture
and optimization investigations remain separate future work. Snap and Homebrew
were not published or certified in this task.

Raw run/log, package, downloaded archive and local installer evidence is retained
in ignored `.build/release-4.9.0/`. Canonical release notes remain immutable.
The established next minor development version is 4.10.0; its version transition
is a separate commit after the verified release and Chocolatey submission.
