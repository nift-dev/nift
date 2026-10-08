# v4.9.0 release-note accuracy audit — 2026-10-09

Reviewed the complete Git delta from immutable v4.8.0 target
`1bfc4a54b373da3477b497106fa4db59111e2770` through accepted investigation
`cc6091334445f266198e3973f6d78f47edf72a09`. Preparation changes are documentation
only; retained runtime remains `3c0a5b22c24da50f749e1f385d84d52ce1a4543d`.

Canonical workflow path is
`docs/evidence/release-4.9.0/release-notes-4.9.0.md`, verified in both publish and
rehearse jobs of `.github/workflows/release.yml`. The reviewed notes are
substantive release notes, not placeholder evidence. Their SHA-256 is
`9b1e33535c82599ea55c647e143b56b21033182ff7903f6f405bdc2bce5c0142`.
`ReleaseNotes.md` carries the same v4.9 body and clearly says not yet published.

| User-facing statement | Source/history cross-check |
|---|---|
| Identity/index/member preparation | `fac9d41`, `62c182e`, `3c0a5b2`; current `ParserExpression.cpp` eligibility/fallback/live slots; 58 selector cases. |
| Owned map/filter/callback/sort transfers | `c9d7a4b`, `3c0a5b2`; current collection callback, decoration and output paths; aggregate independence/stable ties. |
| Ordered group construction index | `26ab438`; current `group_keyed`; instruction-scaling and collision/order controls. |
| Single JSON timer preflight | `7444d2f`; current public `runtime_to_json` and private recursive converter; rejected opaque resources and atomic output unit guards. |
| Exact cached glob ordering keys | `7699b6f`; current `ParserHelpers.cpp`; portable native-path/symlink/matching contracts. |
| Calls, paths, outcome ownership and fingerprints | `3c0a5b2`; `Ast.cpp`, `ParserTemplate.cpp`, `Parser.h`, `ParserExpression.cpp`; consuming Unsupported handler and alias controls. |
| Binding warnings without API changes | `2d83767`, `580f605`; Node/Python wrappers, strict warning helper and maintained aggregate gates. Go/C# were already warning-clean; no fictitious Go/C# source repair is claimed. |
| ABI 1.3 and unchanged public APIs | Public `include/` and Go C-API header diff against v4.8 is empty; unchanged ABI contract guards. |
| No new major language feature | Production delta is bounded performance/warning maintenance and executable version transition. v4.8 workspaces and `warn` are not attributed to v4.9. |
| Preserved observability and deferred work | Both CP49 reports and accepted 19-case identity/capture/worker contract; no optional third-tranche implementation. |

Current `HANDOVER.md`, `PERFORMANCE.md`, README and maintained roadmap now state
v4.9 release preparation, feature freeze ACTIVE and performance campaign CLOSED.
Historical v4.8 evidence, released notes and tag are preserved. The superseded
first-wave official-benchmark handoff is qualified; official measurements are
post-v4.9 only, and `20261008-v480` remains immutable.

Website audit read the source checkout in `nift-dev.github.io`: installation
claims explicitly identify published v4.8.0 (checked 8 October), workspaces are
correctly described as introduced/experimental in v4.8, and performance/dogfood
pages qualify historical measurements. GitHub's latest published release remains
v4.8.0. None requires a premature v4.9 publication claim, and no website rewrite
or Labs edit was performed. The public installer at https://nift.dev/install is
byte-identical to `packaging/install.sh`, SHA-256
`a98bdf72c2c5bd3aed0181f16e0361220b15cf02a2b96d183d98702be6d90708`.

The canonical non-publishing rehearsal defines exactly four portable archives:
Linux x86_64, macOS ARM64, macOS x86_64, Windows x86_64, plus SHA256SUMS.
Linux ARM64 build-only package coverage does not expand that public archive set.
Warnings-as-errors are configured for GCC/Clang, macOS Clang and Windows MinGW;
there is no configured MSVC warning gate. No -Werror policy is weakened.

Final release preparation must repeat local/exact-candidate hosted checks, run
non-publishing rehearsal and inspect its real artifacts. This source/note audit
is not a claim that those subsequent gates have already passed. No tag,
publication, Chocolatey submission, official benchmark or node is authorized.

## Hosted diagnostic runner acquisition

During exact-candidate certification, diagnostic run 37799657123 attempt 1
failed because GitHub never assigned a macOS ARM64 runner. Its macOS job had
no executed steps; the check annotation says: “The job was not acquired by
Runner of type hosted even after multiple attempts.” GitHub also reported
macOS ARM64 capacity constraints. Windows completed successfully. This was
an infrastructure acquisition failure, not a failed or flaky test.

The general compiler diagnostic now uses `macos-26-intel`, already exercised
by the native packaging matrix. This reduces this redundant diagnostic's
reliance on the constrained ARM64 pool. Dedicated cross-platform/FFI gates and
the canonical macOS ARM64 release archive retain native ARM64 coverage. No test,
warning setting, failure handling, release architecture, or runtime code was
changed. The updated diagnostic must pass on the final candidate before
rehearsal; changing runner pools cannot guarantee GitHub service availability.
