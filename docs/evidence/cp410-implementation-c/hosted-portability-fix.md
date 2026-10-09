# Hosted oracle path correction

Initial source commit: `615ffcdcc1bbf48992360fd4f66ce134786d43b8`.
Failed [Checkpoint 10 run](https://github.com/nift-dev/nift/actions/runs/37913485779): macOS reported the physical `/private` temporary directory; Windows reported the native MSYS directory. All observed factory counts, diagnostic positions, error messages and filesystem effects matched; only the exact root normalization differed. The failed log is preserved.

The oracle now resolves its physical fixture root, converts MSYS paths using `cygpath -m` for native CLI arguments and script path operands, and normalizes only those exact physical/native roots, longest first. No oracle outputs, statuses, messages or file effects were relaxed. Every case continues to compare full stdout/stderr/status and filesystem contents. All 97 cases also pass locally with TMPDIR pointing through a symlink alias, exercising the physical-root distinction. The runtime binary is unchanged. This deterministic harness fix is committed before new exact-head hosted certification; the failed run is not rerun unchanged.
