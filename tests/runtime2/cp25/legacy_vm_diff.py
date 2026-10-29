#!/usr/bin/env python3
"""CP25 differential harness: every case runs on legacy (NIFT_VM=0) and on the
VM (NIFT_VM=require, counted through NIFT_VM_STATS) from the same file and the
same directory; exit status, stdout and stderr must be byte-identical and the
VM run must be one VM execution and zero legacy executions.

Cases come from a case file: blocks separated by a line `=== <name>`; the
block's text is the script. Templates expand `{V}` and `{C}` from `--values`
and `--contexts` files (same block format) so a rule can be exercised across
value kinds and execution contexts (top level, function, loop, try, ...).
A case may start with a prelude (top-level declarations) ended by a line
`---`; the prelude is placed before the context, the rest inside it.

"""
import argparse
import itertools
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def blocks(path):
    out, name, lines = [], None, []
    for line in Path(path).read_text().splitlines():
        if line.startswith("=== "):
            if name is not None: out.append((name, "\n".join(lines).strip("\n") + "\n"))
            name, lines = line[4:].strip(), []
        else:
            lines.append(line)
    if name is not None: out.append((name, "\n".join(lines).strip("\n") + "\n"))
    return out


def run(binary, mode, source, td):
    script = Path(td) / "case.f"
    script.write_text(source)
    stats = Path(td) / "stats.jsonl"
    stats.unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(NIFT_VM=mode, NO_COLOR="1", HOME=td)
    if mode != "0": env["NIFT_VM_STATS"] = str(stats)
    p = subprocess.run([binary, "case.f"], cwd=td, env=env, capture_output=True, timeout=60)
    st = {}
    if stats.exists() and stats.read_text().strip():
        st = json.loads(stats.read_text().splitlines()[-1])
    return p.returncode, p.stdout, p.stderr, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--cases", required=True)
    ap.add_argument("--values")
    ap.add_argument("--contexts")
    ap.add_argument("--output")
    ap.add_argument("--or-refused", action="append", default=[],
                    help="a case may instead be refused for a reason with this prefix (an unrelated refusal "
                         "the same construct gets without the corrected rule); counted separately")
    ap.add_argument("--expect-refusal", action="store_true",
                    help="negative control: every case must still be refused by the VM")
    a = ap.parse_args()
    binary = os.path.abspath(a.nift)
    cases = []
    values = blocks(a.values) if a.values else [("-", "")]
    contexts = blocks(a.contexts) if a.contexts else [("-", "{BODY}\n")]
    for (cname, ctext), (vname, vtext), (xname, xtext) in itertools.product(blocks(a.cases), values, contexts):
        text = ctext.replace("{V}", vtext.strip())
        prelude, _, body = text.rpartition("\n---\n") if "\n---\n" in text else ("", "", text)
        source = (prelude.strip() + "\n" if prelude.strip() else "") + xtext.replace("{BODY}", body.strip())
        cases.append((f"{cname}/{vname}/{xname}", source))
    rows, failures, refused_other = [], 0, 0
    for name, source in cases:
        problems = []
        lrc, lout, lerr, _ = run_norm(binary, "0", source)
        vrc, vout, verr, st = run_norm(binary, "require", source)
        refused_ok = st.get("outcome") not in ("vm", "vm-error") and any(
            st.get("reason", "").startswith(r) for r in a.or_refused)
        if refused_ok:
            refused_other += 1
        elif a.expect_refusal:
            if st.get("outcome") == "vm": problems.append("VM executed a case expected to be refused")
        else:
            if (st.get("vm_executions"), st.get("legacy_executions")) != (1, 0):
                problems.append(f"VM run outcome {st.get('outcome')} {st.get('reason', '')}")
            if (lrc, lout, lerr) != (vrc, vout, verr):
                problems.append(f"differs: legacy rc={lrc} out={lout!r} err={lerr!r} | vm rc={vrc} out={vout!r} err={verr!r}")
        rows.append({"case": name, "source": source, "problems": problems,
                     "legacy": [lrc, lout.decode(errors="replace"), lerr.decode(errors="replace")],
                     "vm": [vrc, vout.decode(errors="replace"), verr.decode(errors="replace")]})
        if problems:
            failures += 1
            print(f"FAIL {name}: " + "; ".join(problems)[:600])
    if a.output: Path(a.output).write_text(json.dumps(rows, indent=1) + "\n")
    extra = f" ({refused_other} refused as allowed: {', '.join(a.or_refused)})" if a.or_refused else ""
    print(f"{'PASS' if not failures else 'FAIL'} legacy/VM differential: {len(rows)} cases, {failures} failure(s){extra}")
    return 1 if failures else 0


def run_norm(binary, mode, source):
    """Run in a fixed-name directory so paths in diagnostics are identical."""
    base = Path(tempfile.gettempdir()) / f"cp25-diff-{os.getpid()}"
    if base.exists():
        for f in base.iterdir(): f.unlink()
    base.mkdir(exist_ok=True)
    try:
        rc, out, err, st = run(binary, mode, source, str(base))
    finally:
        for f in base.iterdir(): f.unlink()
        base.rmdir()
    return rc, out, err, st


if __name__ == "__main__":
    sys.exit(main())
