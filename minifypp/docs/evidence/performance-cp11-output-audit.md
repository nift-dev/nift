# Performance CP11 — scanner/output audit

The profiling harness now accepts `--mode default|structured|aggressive` so stage traces can be isolated by policy instead of interleaving all three modes.

On the 276,949-byte generated aggressive-friendly probe, an isolated structured run attributed roughly 5–6 ms of the first pass to scanner/output emission versus roughly 6–8 ms concrete syntax, 6–8 ms scope construction, 3–4 ms reference resolution, plus binding-renaming/rewrite work outside the coarse stage timers. The scanner already reserves the output buffer to input size and reserves token storage when structural collection is enabled.

No production printer/scanner change is retained at this checkpoint. The profile does not justify trading scanner correctness for a small fraction of structured/aggressive runtime while analysis and iterative optimization remain larger targets.
