# GitHub Actions commit pins (D-104)

Owner decision D-104 approved pinning every GitHub Actions reference in `.github/workflows` to a full commit SHA. Each
pin is written as `uses: owner/repo@<40-char sha> # <tag>`. The change touches only `uses:` lines. Every major-version
tag was pinned to the commit it pointed to when it was resolved, so the workflows run the same action version as before.
No workflow was run to make or check this change.

**How each tag was resolved** (2026-11-14 17:24 UTC, read-only queries only):

- **(1)** `git ls-remote --tags https://github.com/<owner>/<repo>`: the full tag list. None of the eight tags has a peeled
  `^{}` entry, so each is a lightweight tag that points directly at a commit.
- **(2)** A second, separate `git ls-remote https://github.com/<owner>/<repo> refs/tags/<tag> refs/tags/<tag>^{}`.
- **(3)** The GitHub REST API, read through WebFetch:
  - `GET /repos/<owner>/<repo>/git/ref/tags/<tag>` for most tags, where `object.type` must be `commit`;
  - `git/matching-refs/tags/<tag>` for checkout and setup-python;
  - `GET /repos/actions/upload-artifact/commits/v7` for upload-artifact, because both ref endpoints failed for it.

All three sources agreed for every tag. None was left unresolved.

| Old reference | Resolved commit | Exact release on the same commit | Sources | Date |
|---|---|---|---|---|
| `actions/checkout@v5` | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` | v5.1.0 | (1) (2) (3) | 2026-11-14 |
| `actions/setup-python@v6` | `ece7cb06caefa5fff74198d8649806c4678c61a1` | v6.3.0 | (1) (2) (3) | 2026-11-14 |
| `actions/upload-artifact@v7` | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` | v7.0.2 | (1) (2) (3) | 2026-11-14 |
| `msys2/setup-msys2@v2` | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` | v2.33.0 | (1) (2) (3) | 2026-11-14 |
| `actions/setup-node@v7` | `949feb2413d6458794dcd2491c4babbbce0c15c1` | v7.1.0 | (1) (2) (3) | 2026-11-14 |
| `actions/setup-go@v7` | `b7ad1dad31e06c5925ef5d2fc7ad053ef454303e` | v7.0.0 | (1) (2) (3) | 2026-11-14 |
| `actions/setup-dotnet@v6` | `0f4a354dd5547a28c70b7853dbd95d5b67886e73` | v6.1.0 | (1) (2) (3) | 2026-11-14 |
| `ilammy/msvc-dev-cmd@v1` | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` | v1.13.0 | (1) (2) (3) | 2026-11-14 |

## Where each pin was applied

The table lists each workflow file, the reference it used before, how many lines changed, and the commit pinned. All
sources and dates are as in the table above.

| Workflow file | Old reference | Lines | Pinned commit |
|---|---|---|---|
| checkpoint-10-cross-platform.yml | `actions/checkout@v5` | 2 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| checkpoint-10-cross-platform.yml | `actions/setup-python@v6` | 2 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| checkpoint-10-cross-platform.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| checkpoint-10-cross-platform.yml | `actions/upload-artifact@v7` | 4 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| e3b-sidecar-certification.yml | `actions/checkout@v5` | 4 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| e3b-sidecar-certification.yml | `actions/setup-python@v6` | 2 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| e3b-sidecar-certification.yml | `msys2/setup-msys2@v2` | 2 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| e3b-sidecar-certification.yml | `actions/upload-artifact@v7` | 2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| gate6ar-ffi.yml | `actions/checkout@v5` | 2 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| gate6ar-ffi.yml | `actions/setup-node@v7` | 2 | `949feb2413d6458794dcd2491c4babbbce0c15c1` |
| gate6ar-ffi.yml | `actions/setup-python@v6` | 2 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| gate6ar-ffi.yml | `actions/setup-go@v7` | 1 | `b7ad1dad31e06c5925ef5d2fc7ad053ef454303e` |
| gate6ar-ffi.yml | `actions/setup-dotnet@v6` | 1 | `0f4a354dd5547a28c70b7853dbd95d5b67886e73` |
| gate6ar-ffi.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| gate6b-bytes.yml | `actions/checkout@v5` | 3 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| gate6b-bytes.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| hosted-certification-diagnostic.yml | `actions/checkout@v5` | 1 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| hosted-certification-diagnostic.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| init-targets.yml | `actions/checkout@v5` | 1 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| init-targets.yml | `actions/setup-python@v6` | 1 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| init-targets.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| lambda-metadata-certification.yml | `actions/checkout@v5` | 3 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| lambda-metadata-certification.yml | `actions/setup-python@v6` | 2 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| lambda-metadata-certification.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| lambda-metadata-certification.yml | `actions/upload-artifact@v7` | 2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| lambda-metadata-certification.yml | `ilammy/msvc-dev-cmd@v1` | 1 | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` |
| nightly-deep.yml | `actions/checkout@v5` | 8 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| nightly-deep.yml | `actions/setup-python@v6` | 5 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| packaging.yml | `actions/checkout@v5` | 2 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| packaging.yml | `actions/setup-go@v7` | 2 | `b7ad1dad31e06c5925ef5d2fc7ad053ef454303e` |
| packaging.yml | `actions/upload-artifact@v7` | 2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| packaging.yml | `actions/setup-dotnet@v6` | 1 | `0f4a354dd5547a28c70b7853dbd95d5b67886e73` |
| packaging.yml | `actions/setup-node@v7` | 1 | `949feb2413d6458794dcd2491c4babbbce0c15c1` |
| process-hardening.yml | `actions/checkout@v5` | 1 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| process-hardening.yml | `actions/setup-python@v6` | 1 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| process-hardening.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| process-hardening.yml | `actions/upload-artifact@v7` | 1 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| runtime2-cross-platform.yml | `actions/checkout@v5` | 1 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| runtime2-cross-platform.yml | `actions/setup-python@v6` | 1 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| runtime2-cross-platform.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| runtime2-cross-platform.yml | `actions/upload-artifact@v7` | 1 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| runtime2-regression-suite.yml | `actions/checkout@v5` | 9 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| scalar-s0-certification.yml | `actions/checkout@v5` | 3 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| scalar-s0-certification.yml | `actions/setup-python@v6` | 3 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| scalar-s0-certification.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| scalar-s0-certification.yml | `actions/upload-artifact@v7` | 3 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| scalar-s0-certification.yml | `ilammy/msvc-dev-cmd@v1` | 1 | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` |
| sort-number-certification.yml | `actions/checkout@v5` | 3 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| sort-number-certification.yml | `actions/setup-python@v6` | 3 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| sort-number-certification.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| sort-number-certification.yml | `ilammy/msvc-dev-cmd@v1` | 1 | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` |
| template-p0-certification.yml | `actions/checkout@v5` | 1 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| template-p0-certification.yml | `actions/setup-python@v6` | 1 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| template-p0-certification.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |
| template-p0-certification.yml | `actions/upload-artifact@v7` | 1 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| test-integrity.yml | `actions/checkout@v5` | 4 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| test-integrity.yml | `actions/setup-python@v6` | 1 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| test-integrity.yml | `actions/setup-go@v7` | 1 | `b7ad1dad31e06c5925ef5d2fc7ad053ef454303e` |
| test-integrity.yml | `actions/setup-dotnet@v6` | 1 | `0f4a354dd5547a28c70b7853dbd95d5b67886e73` |
| test-integrity.yml | `actions/setup-node@v7` | 1 | `949feb2413d6458794dcd2491c4babbbce0c15c1` |
| v44-cross-platform.yml | `actions/checkout@v5` | 8 | `fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09` |
| v44-cross-platform.yml | `actions/setup-python@v6` | 1 | `ece7cb06caefa5fff74198d8649806c4678c61a1` |
| v44-cross-platform.yml | `msys2/setup-msys2@v2` | 1 | `ec48f7c5447b3140e2b088413ae3a55687bccb6e` |

## Pins that were already in place (checked, not changed)

The packaging and distribution workflows already used commit SHAs. Only the Homebrew pin carries a tag comment. For
each of the others, the tag that points at its SHA was looked up with sources (1) and (2):

| Action | Pinned SHA | Tag(s) pointing at it now | Result |
|---|---|---|---|
| `actions/checkout` | `fbc6f3992d24…` | v5, v5.1.0 | Same commit as the new v5 pin. |
| `actions/setup-python` | `ece7cb06caef…` | v6, v6.3.0 | Same commit as the new v6 pin. |
| `actions/upload-artifact` | `043fb46d1a93…` | v7.0.1 | Valid, but older than v7, which is now v7.0.2 (`cf430e03`). The new `@v7` pins therefore run v7.0.2 and the existing ones v7.0.1. |
| `actions/download-artifact` | `37930b1c2abaa49bbe596cd826c3c89aef350131` | v7, v7.0.0 | Matches. |
| `msys2/setup-msys2` (release.yml) | `66cd2cce69ca…` | v2.32.0 | Valid, but older than v2, which is now v2.33.0 (`ec48f7c5`). |
| `Homebrew/actions/setup-homebrew` | `df4b09108a1de9d6f995fe68f302b3f68bd6d2ef` | 2026.07.20.1 | **Matches its comment** (`# 2026.07.20.1`). |
| `snapcore/action-build` (snap.yml) | `b391f430cb2650fd359ed4d2cfe88b2c481800e0` | v1, but as an **annotated tag object** | **Not a commit.** `v1^{}` peels to commit `3bdaa03e1ba6bf59a65f84a751d943d549a54e79`, and the API reports `object.type: tag`. Left unchanged under D-104 rule 3; reported for an owner decision. |
