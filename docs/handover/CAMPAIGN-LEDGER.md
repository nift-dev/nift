# Nift Battle Hardening campaign — compact ledger

One row per checkpoint, appended at the end of each BH. Exact immutable commits
are preserved so the reviewer can attack each boundary independently. Nick
reserves semantic decisions, guarantee downgrades and roadmap changes.

| BH | Commit | Guarantee / scope | Attacks performed | Findings | Guards added/strengthened | Red-run evidence | Tests run | Known limitations | Roadmap notes |
|----|--------|-------------------|-------------------|----------|---------------------------|------------------|-----------|-------------------|---------------|
| BH1 | `8419cec` | Guarantee registry + baseline map | Reviewer (DeepSeek) attacked ChatGPT's checker | closed | `docs/guarantees/registry.json`, `scripts/check_guarantee_registry.py`, `scripts/bh1_registry_liveness.py` | `docs/evidence/bh1/` | registry local/full, liveness battery | registry wording audit deferred to BH10 | — |
| BH2 | `84816dc` (close `d049b24`) | Test integrity + enforcement architecture (9 review rounds) | ChatGPT rounds 1–9: layout, pipefail state/env/identity/resolution, paths negation/glob/representation, fail-closed triggers | closed with documented static-analysis boundaries | `scripts/test_integrity_check.py`, `scripts/check_guarantee_registry.py` hardening | `docs/evidence/bh2/` (9 fixture families) | clean 42 + contract 21, registry CI/full, liveness, CI-equivalent chain, time-family SKIP(2) | sourced/runtime-generated shell is NOT_ESTABLISHED, fails conservatively; general Bash/YAML semantics out of scope | BH3 owns runtime mutation |
| BH3 | `50f7d7f` (t1), pending (t2) | Curated guard mutation / test-of-test | t1: pagination m2 stub + m4 check-removal LIVE_FALSE_GREEN; contracts robust to stub; BH2 scanner blind to logic mutations | see handover | `scripts/bh3_guard_mutation.py`; pagination live-output assertion (t2) | `docs/evidence/bh3/bh3-mutation-tranche1.json` (+ t2) | guard baselines, BH2 batteries, registry CI | logic-mutation removal not statically catchable (enforcement boundary) | BH4 broadens incremental state-space |
