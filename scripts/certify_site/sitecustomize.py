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
        except Exception as e:  # never hide or crash the harness: the line names the failure (review of 4ec1aa80)
            sys.stderr.write("CERTIFY-RTDIFF-ERROR %s\n" % e)
        return _real_rmtree(path, *args, **kwargs)

    shutil.rmtree = _keeping_rmtree

# C36 (test-runtime2-cp14b): the cp14b harnesses (functional.py, effects.py, origins.py) print
# only `FAIL <name>`; the failing case's evidence goes to their --output JSON. At exit, the JSON
# they wrote is read and each failing result printed to stderr, in name order:
#     CERTIFY-CP14B <script>:<name> <detail>
# (detail: the VM's exit status, refusal reason and first error line, and legacy's exit status;
# or the harness's own `detail` field), then `CERTIFY-CP14B-SUMMARY <script> failed=N` counted
# from the JSON (independent of the FAIL lines). Behaviour, output and exit status are unchanged.
if os.environ.get("CERTIFY_CP14B_DETAIL") and os.path.basename(sys.argv[0] if sys.argv else "") in (
        "functional.py", "effects.py", "origins.py"):
    import atexit

    def _cp14b_detail():
        script = os.path.basename(sys.argv[0])
        try:
            out = sys.argv[sys.argv.index("--output") + 1]
            with open(out) as f:
                data = json.load(f)
            results = data.get("results", []) if isinstance(data, dict) else data
            failed = sorted((r for r in results if isinstance(r, dict) and not r.get("pass_")),
                            key=lambda r: str(r.get("name")))
            for r in failed:
                vm, legacy = r.get("vm"), r.get("legacy")
                if isinstance(vm, dict):
                    st = (vm.get("stats") or [{}])[-1]
                    errs = [l for l in str(vm.get("err", "")).splitlines() if l.startswith("error:")]
                    detail = "vm exit=%s outcome=%s reason=%s err=%s legacy exit=%s" % (
                        vm.get("exit"), st.get("outcome"), st.get("reason") or "-", errs[0] if errs else "-",
                        legacy.get("exit") if isinstance(legacy, dict) else "-")
                else:
                    detail = json.dumps(r.get("detail"), sort_keys=True)[:400]
                sys.stderr.write("CERTIFY-CP14B %s:%s %s\n" % (script, r.get("name"), detail))
            sys.stderr.write("CERTIFY-CP14B-SUMMARY %s failed=%d\n" % (script, len(failed)))
        except Exception as e:  # never hide or crash the harness
            sys.stderr.write("CERTIFY-CP14B-ERROR %s %s\n" % (script, e))
        sys.stderr.flush()

    atexit.register(_cp14b_detail)
