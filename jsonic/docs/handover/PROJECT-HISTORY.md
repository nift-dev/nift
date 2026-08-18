# Jsonic++ project history

Jsonic++ was extracted in August 2026 from the small JSON implementation already shared byte-for-byte by Nift core and Minify++. The extraction preserves that implementation rather than rewriting it for branding. The project exists so JSON correctness, conformance, fuzzing and memory-safety evidence can be owned independently while Nift and Minify++ retain vendored dependency-free copies.

## 2026-08-18 — Memory-safety Checkpoint 1B independent confirmation

The maintained lifetime corpus was independently run under Valgrind 3.26.0 on Linux at commit `b9d0ff3`. Forty corpus iterations completed with 0 errors, 0 bytes in use at exit, all 6,579,515 allocations freed and no leaks possible. Peak Valgrind process RSS was 215,992 KiB. This completed the Jsonic++ memory/lifetime checkpoint without requiring a production parser fix; the machine-readable evidence is retained in `docs/evidence/memory-safety-checkpoint-1b-valgrind.json`.
