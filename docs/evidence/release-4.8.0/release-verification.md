# NIFT v4.8.0 — GITHUB RELEASE FINAL

Published: YES. Release blockers: NONE.

- Release: https://github.com/nift-dev/nift/releases/tag/v4.8.0
- Annotated tag: `v4.8.0`; object `eb6b8e0a77bbbc41acd9e3c40de324c1b9e10055`.
- Exact target: `1bfc4a54b373da3477b497106fa4db59111e2770`.
- Publishing workflow: https://github.com/nift-dev/nift/actions/runs/37746497666 — PASS.
- Final release, not draft/prerelease; reviewed body matches canonical notes (SHA-256 `d7d8d4f8a8eb646e21ab5356c2cb62a4ba1bacab739fc8cbe41061eba23d8e29`).
- Exactly five public assets: four portable archives and SHA256SUMS. No certification assets; Linux ARM64 remains separate build certification.
- Every archive: checksum, architecture, exact binary/README/LICENSE contents, and native extracted-archive version/init/build/status PASS. Windows static-runtime imports PASS.
- Three canonical public installer lanes PASS. Additional actual-public Linux install/repeat/init/build and isolated bad-checksum refusal/existing-install preservation PASS. No user-home modification.
- Website release status PASS: source stage `740b1d5`, public main `867e5d8`, Pages run #37747374482 PASS; live homepage/install/commands/discovery/installer byte-identical.

The workflow rebuilds the exact tagged source rather than publishing rehearsal bytes. All four archive checksums differ due to regenerated archive/build metadata. Linux and both macOS executables are byte-identical to rehearsal. Windows differs at only four bytes, confined to the PE timestamp and derived PE checksum. Public emitted SHA256SUMS and native smoke results independently pass.

| Public archive | Bytes | SHA-256 |
|---|---:|---|
| nift-4.8.0-linux-x86_64.tar.gz | 1873041 | `67caadea435ea68afbca74579368c28aa1d5bc6d3e3a4558d8c435fc32bc47a5` |
| nift-4.8.0-macos-arm64.tar.gz | 1545471 | `a0b16280b42fda6c99f867650c33a331a57696d56b1e1a8d15fbecbb5209a4f7` |
| nift-4.8.0-macos-x86_64.tar.gz | 1633819 | `e986ceb2b26267ce57d6b2963064aef2641c6d6f43db977c0bee65ea50425269` |
| nift-4.8.0-windows-x86_64.zip | 2664597 | `bb7d89d5e564adc32b503c73a8a412bf93910a774353c90dcb70e5fdb53dcfdf` |

Detailed raw evidence is retained in `.build/release48-public/` (run, release metadata, workflow log, archive inspection, smoke logs, website checks and rehearsal comparisons).

## Chocolatey and downstream status

Chocolatey 4.8.0 submitted successfully: https://github.com/nift-dev/nift/actions/runs/37747611875 (exact release source). The push step explicitly confirms `nift.4.8.0.nupkg was pushed successfully`. Retained `.nupkg` inspection confirms version, final GitHub Windows archive URL and checksum, canonical install helper and verification metadata. No template/checksum source change was necessary.

Public package: https://community.chocolatey.org/packages/nift/4.8.0 — pending moderation; validation pending, verification pending, scan pending/unknown at inspection. This is submitted, not approved or certified publicly installable. No disposable Windows VM was available for an additional local package install/upgrade/uninstall test; canonical Windows release smoke and package staging executable version checks passed. Uninstall uses standard Chocolatey auto-uninstaller metadata, with no custom script.

Snap: pending — maintainer-managed by Nick. Homebrew: automatic downstream propagation — not checked in this task. Flathub: out of scope.

Deferred work (not implemented): bindings warnings and support reassessment/formal publication; fresh formal scripting and shell benchmarks; dedicated-node lab.nift.dev update and evaluation of another performance campaign; closure/capture/storage investigation if justified; possible future explicit FFI release/lifetime design.
