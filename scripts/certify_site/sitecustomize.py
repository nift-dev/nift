"""certify.py runner (owner 2026-11-08 03:27, test-rt-vm parity): imported by every Python
process of a gate whose extractor asks for it (PYTHONPATH), on the candidate and on every baseline
tree alike; the trees' harnesses are not changed.

vm_parity.py writes rt-diff's full report into a temporary directory and removes it, so its log
says only `FAIL vm parity: N failure(s)`. Here `shutil.rmtree` is wrapped: before a directory is
removed, an rt-diff-report.json in it is copied to $CERTIFY_KEEP_DIR (outside the deleted
directory) and every unapproved difference in it is printed to stderr, one line per entry and
channel, in a fixed order:
    CERTIFY-RTDIFF <entry id> <channel> <first difference as JSON>
Nothing else about the harness's behaviour, output or exit status changes.
"""
import json
import os
import shutil
import sys

_KEEP = os.environ.get("CERTIFY_KEEP_DIR")
if _KEEP:
    _real_rmtree = shutil.rmtree
    _count = [0]

    def _keeping_rmtree(path, *args, **kwargs):
        try:
            report = os.path.join(path, "rt-diff-report.json")
            if os.path.isfile(report):
                os.makedirs(_KEEP, exist_ok=True)
                _count[0] += 1
                shutil.copyfile(report, os.path.join(_KEEP, f"{os.getpid()}-{_count[0]:03d}-rt-diff-report.json"))
                with open(report) as f:
                    data = json.load(f)
                for entry in sorted(data.get("results", []), key=lambda r: r.get("id", "")):
                    for d in sorted(entry.get("diffs") or [], key=lambda d: d.get("channel", "")):
                        sys.stderr.write("CERTIFY-RTDIFF %s %s %s\n" % (
                            entry.get("id"), d.get("channel"), json.dumps(d.get("first"), sort_keys=True)))
                sys.stderr.write("CERTIFY-RTDIFF-SUMMARY differing=%s approved=%s entries=%s\n" % (
                    data.get("summary", {}).get("differing"), data.get("summary", {}).get("approved"),
                    data.get("summary", {}).get("entries")))
                sys.stderr.flush()
        except (OSError, ValueError) as e:  # never hide: the line names the failure
            sys.stderr.write("CERTIFY-RTDIFF-ERROR %s\n" % e)
        return _real_rmtree(path, *args, **kwargs)

    shutil.rmtree = _keeping_rmtree
