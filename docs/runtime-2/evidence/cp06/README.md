# CP06 evidence (2026-10-17, Linux x86_64, 2 vCPU)

## Instructions, 300-page Markdown site (callgrind)

Reference: runtime-2 binary built from `a02072e` (sources identical to
`e27d071`). Measured in the same session with the same script (copy of the
site, `.nift/public` removed, `build --all` then a no-op `build`).

| Binary | full build | no-op build |
| --- | ---: | ---: |
| runtime-2 (reference) | 849,259,666 | 114,680,641 |
| `parse` probes only (intermediate) | 824,234,587 (−2.9%) | 114,684,071 |
| CP06 | 358,140,620 (**−57.8%**) | 114,684,105 (+0.003%) |

Gates: full build ≥ 30% fewer — met; no-op not worse than +1% — met.

## Parity

`NIFT_REF=<runtime-2 nift> NIFT_SITE=<300-page site> tests/runtime2/scanner_parity.sh`:
73 sites (12 repository sites, 60 generated, the 300-page site) and 121
scripts compared, no differences. Mutation check: disabling the `-->`
handling in `append_indented` produced 44 failing sites.

## make -k test (NIFT_REF set, so the parity target ran)

Failing targets: test-dependency-status, test-progress-pty,
test-unreadable-source, test-v43-language,
test-v48-prepared-collection-guard, test-v48-prepared-object-literal-guard —
exactly the environmental set recorded since CP00 (root user, no
/usr/bin/time, no pty). All `test-runtime2-*` targets passed.
