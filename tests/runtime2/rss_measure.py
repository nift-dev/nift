"""Native time(1) options and peak RSS units used by Runtime 2.0 tests."""
import re
import sys


def time_args(darwin):
    return ["-l"] if darwin or sys.platform.startswith("freebsd") else ["-f", "%M"]


def peak_rss_bytes(stderr, darwin):
    if darwin or sys.platform.startswith("freebsd"):
        matches = re.findall(rb"^[ \t]*(\d+)[ \t]+maximum resident set size[ \t]*$", stderr, re.M)
        return (int(matches[-1]) * (1 if darwin else 1024) or None) if matches else None
    lines = stderr.strip().splitlines()
    return (int(lines[-1]) * 1024 or None) if lines and lines[-1].isdigit() else None
