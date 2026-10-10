# v4.11 release-note accuracy audit

Canonical notes are reviewed against the accepted implementation and executable
contracts. Release readiness must additionally certify the final preparation SHA;
prior results support the claims but do not replace that certification.

| Material claim | Authority |
|---|---|
| Per-consumer history and targeted omission correctness | `src/BuildState.cpp`, `src/ProjectInfo.cpp`; `tests/consumer_dependency_snapshots.py`; accepted v411-resume closeout/matrix |
| Exact native consumed-byte authority | Native dependency recording in `src/Parser.cpp`, `src/ParserTemplate.cpp`, `src/ProjectInfo.cpp`; conflicting-read/native barrier and declared FileValue hook contracts |
| v4.10 migration and fingerprint compatibility | `tests/consumer_dependency_snapshots.py` migration/fingerprint cases; hash/hybrid rebuild once, modified remains timestamp-based |
| Generated dependencies and prerequisite closure | `tests/v410_build_pipeline_smoke.sh`; snapshot generated variants and final differential smoke |
| Parent-prepared POSIX launch state / cleanup | `src/PreparedProcessPOSIX.h`, `src/ProcessPOSIX.h`, `src/Process.cpp`, `src/JobControl.cpp`; process contracts/faults and5/5 mutants |
| Windows env isolation / handle list / redirects / Unicode | `src/Process.cpp`; `tests/process_contract.cpp`, `tests/process_failure.cpp`; native30-case process receipt |
| Native platform coverage | Process run38038408869, checkpoint38038408828, shell/runtime38039057855; `docs/evidence/v411-process/hosted-*.json` |
| NRS94 / PRS12 | Accepted exact-binary logs in v411-process; NRS6da99128 hosted38035852958; PRS1da43659; final candidate runs required |
| ABI1.3 unchanged | `include/nift/c_abi.h`; no public ABI change in v4.11 |
| Trusted-code / stable-input boundary | README.md incremental/trusted-code contract; opaque external ABA explicitly excluded; no hostile-repository sandbox or cryptographic FNV claim |

No release note claims arbitrary external ABA safety, recursive descendant
ownership, Windows interactive jobs, universal external environment-writer safety,
security proof, formal verification or new official benchmark rankings.
