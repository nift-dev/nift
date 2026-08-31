#!/usr/bin/env python3
"""Regression benchmark for full-build output scaling (repaired methodology).

Protects against doing directory-wide recovery or validation once per generated
file: the Checkpoint 8 transactional-writer regression made a flat N-page build
O(N^2) by scanning the output directory before every write.

Repaired methodology (v4.0.8):
  - Larger fixtures (2,000 and 8,000 pages, a 4:1 size relationship) so each
    measured build runs long enough to be observable.
  - Both fixtures are created and warmed (at least one untimed full rewrite of
    each) before any timed sampling; fixture construction, initial output
    creation and filesystem-cache warm-up are never timed.
  - At least seven balanced interleaved timed rounds. Round i starts with the
    small fixture for even i and the large fixture for odd i, cancelling
    shared-runner load drift. Each fixture's template is toggled before every
    timed build so every timed build rewrites every output through the real
    transactional write path (never an identical-output fast path).
  - The median of the seven paired large/small ratios is the primary decision
    value (threshold 7.00x). Raw samples are always printed.
  - One predeclared confirmation phase runs only when the primary median
    exceeds the threshold: another full set of balanced rounds with the
    starting order reversed. The run fails only if the confirmation median also
    exceeds the threshold - a single contaminated phase cannot fail the release,
    while repeatable poor scaling does. This is not retry-until-green.

The benchmark fails closed if a timed build returns non-zero, if the expected
output count is incomplete, or if any timed build does not rewrite all expected
outputs (every generated file must carry the current template variant).
"""
import argparse
import json
import pathlib
import statistics
import subprocess
import sys
import tempfile
import time

ap = argparse.ArgumentParser()
ap.add_argument("--nift", required=True)
ap.add_argument("--small", type=int, default=2000)
ap.add_argument("--large", type=int, default=8000)
ap.add_argument("--rounds", type=int, default=7)
ap.add_argument("--confirm-rounds", type=int, default=7)
ap.add_argument("--max-ratio", type=float, default=7.0,
                help="maximum median large/small paired ratio; 8k/2k linear scaling is ~4x")
args = ap.parse_args()

VARIANTS = ("<main class='a'>@content</main>\n", "<main class='b'>@content</main>\n")
# The fragment that appears in generated output for each variant (the template
# string itself contains @content, which is replaced during rendering).
OUTPUT_MARKERS = (b"<main class='a'>", b"<main class='b'>")


def fixture(root, n):
    (root / ".nift").mkdir(parents=True)
    (root / "content").mkdir()
    (root / "templates").mkdir()
    (root / "public").mkdir()
    (root / ".nift/config.json").write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/template.html",
        "build-threads": 1, "incremental-mode": "modified", "minify-exts": []}}))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [
        {"name": "p%d" % i, "title": "P%d" % i, "template": "templates/template.html"}
        for i in range(n)]}, separators=(",", ":")))
    (root / "templates/template.html").write_text(VARIANTS[0])
    for i in range(n):
        (root / "content" / ("p%d.html" % i)).write_text("<p>%d</p>\n" % i)


class Fixture:
    def __init__(self, root, n):
        self.root = pathlib.Path(root)
        self.n = n
        self.template = self.root / "templates/template.html"
        self.variant = 0

    def toggle(self):
        self.variant = 1 - self.variant
        self.template.write_text(VARIANTS[self.variant])

    def verify_rewrite(self):
        marker = OUTPUT_MARKERS[self.variant]
        outputs = sorted((self.root / "public").glob("*.html"))
        if len(outputs) != self.n:
            raise SystemExit("FAIL: expected %d outputs, found %d" % (self.n, len(outputs)))
        for out in outputs:
            if marker not in out.read_bytes():
                raise SystemExit("FAIL: %s was not rewritten with the current template variant" % out.name)

    def timed_build(self):
        self.toggle()
        started = time.perf_counter()
        p = subprocess.run([args.nift, "build", "--all"], cwd=str(self.root),
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        elapsed = time.perf_counter() - started
        if p.returncode:
            raise SystemExit(p.stderr.decode(errors="replace"))
        self.verify_rewrite()
        return elapsed

    def warm(self):
        # Untimed full rewrites of both variants: initial output creation and
        # filesystem-cache warm-up are excluded from timed sampling.
        self.toggle()
        subprocess.run([args.nift, "build", "--all"], cwd=str(self.root),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
        self.toggle()
        subprocess.run([args.nift, "build", "--all"], cwd=str(self.root),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)


def collect(fx_small, fx_large, rounds, start_large=False):
    """Balanced interleaved rounds; returns (small_durations, large_durations)."""
    smalls, larges = [], []
    for i in range(rounds):
        if (i % 2 == 0) != start_large:
            s, l = fx_small.timed_build(), fx_large.timed_build()
        else:
            l, s = fx_large.timed_build(), fx_small.timed_build()
        smalls.append(s)
        larges.append(l)
    return smalls, larges


def report(title, smalls, larges):
    ratios = [l / s for s, l in zip(smalls, larges)]
    print("--- %s ---" % title)
    print("fixture sizes: small=%d large=%d  rounds=%d  max-ratio=%.2fx"
          % (args.small, args.large, len(smalls), args.max_ratio))
    print("small raw durations (s): %s" % ["%.6f" % d for d in smalls])
    print("large raw durations (s): %s" % ["%.6f" % d for d in larges])
    print("paired large/small ratios: %s" % ["%.2f" % r for r in ratios])
    print("median small: %.6fs  (min %.6f, max %.6f)"
          % (statistics.median(smalls), min(smalls), max(smalls)))
    print("median large: %.6fs  (min %.6f, max %.6f)"
          % (statistics.median(larges), min(larges), max(larges)))
    print("median paired ratio: %.2fx" % statistics.median(ratios))
    return statistics.median(ratios)


def main():
    with tempfile.TemporaryDirectory(prefix="nift-full-scale-small-") as td_s, \
         tempfile.TemporaryDirectory(prefix="nift-full-scale-large-") as td_l:
        fixture(pathlib.Path(td_s), args.small)
        fixture(pathlib.Path(td_l), args.large)
        fx_small = Fixture(td_s, args.small)
        fx_large = Fixture(td_l, args.large)
        fx_small.warm()
        fx_large.warm()

        primary = report("primary phase", *collect(fx_small, fx_large, args.rounds))
        if primary <= args.max_ratio:
            print("PASS: primary phase (median %.2fx <= %.2fx)" % (primary, args.max_ratio))
            return 0

        print("CONFIRMATION REQUIRED: primary median %.2fx exceeds %.2fx; running one "
              "predeclared confirmation phase with the starting order reversed"
              % (primary, args.max_ratio))
        confirmation = report("confirmation phase",
                              *collect(fx_small, fx_large, args.confirm_rounds, start_large=True))
        if confirmation > args.max_ratio:
            print("FAIL: repeated scaling violation (primary %.2fx, confirmation %.2fx > %.2fx)"
                  % (primary, confirmation, args.max_ratio))
            return 1
        print("PASS: confirmation disproved initial noisy result "
              "(confirmation median %.2fx <= %.2fx)" % (confirmation, args.max_ratio))
        return 0


if __name__ == "__main__":
    sys.exit(main())