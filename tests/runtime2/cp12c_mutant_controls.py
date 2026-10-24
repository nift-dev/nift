#!/usr/bin/env python3
"""Negative controls for both mutation gates.

Every fake receives a provenance manifest that matches its own hash, so this
exercises the semantic/telemetry checks rather than only the provenance gate.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile


def run(script, nift, normal, info, mutant):
    p = subprocess.run([sys.executable, script, "--nift", nift, "--normal", normal,
                        "--mutant-info", info, "--mutant", str(mutant)],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=90)
    return p.returncode, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--normal", required=True)
    a = ap.parse_args()
    normal = os.path.abspath(a.normal)
    here = os.path.dirname(os.path.abspath(__file__))
    failures = 0
    with tempfile.TemporaryDirectory(prefix="cp12c-controls-") as d:
        bodies = {
            "fakeheap": "import os,json\nopen(os.environ['NIFT_VM_STATS'],'w').write(json.dumps({'outcome':'vm','vm_executions':1,'heap_live':999999999}))\nsys.exit(0)\n",
            "unrelated": "import os,json\nopen(os.environ['NIFT_VM_STATS'],'w').write(json.dumps({'outcome':'vm-error','vm_executions':1,'reason':'unrelated failure'}))\nsys.exit(1)\n",
            "timeout": "import time\ntime.sleep(10)\n",
            "exit137": "sys.exit(137)\n",
            "malformed": "import os\nopen(os.environ['NIFT_VM_STATS'],'w').write('{}')\nsys.exit(1)\n",
        }
        for label, body in bodies.items():
            fake = os.path.join(d, label)
            with open(fake, "w") as f:
                f.write("#!/usr/bin/python3\nimport sys\n" + body)
            os.chmod(fake, 0o755)
            digest = hashlib.sha256(open(fake, "rb").read()).hexdigest()
            normal_digest = hashlib.sha256(open(normal, "rb").read()).hexdigest()
            info = os.path.join(d, label + ".json")
            json.dump({"mutant": 1, "sha256": digest, "build": "for-mutant",
                       "normal_sha256": normal_digest}, open(info, "w"))
            for script, mutant in (("for_s2.py", 1), ("for_s2.py", 5), ("closures_s1.py", 1)):
                rc, out = run(os.path.join(here, script), fake, normal, info, mutant)
                if rc == 0 or "PASS" in out:
                    failures += 1
                    print(f"FAIL control {label}/{script}/M{mutant} was credited")
                    print(out[-1200:])
            # Missing provenance must fail too.
            for script, mutant in (("for_s2.py", 5), ("closures_s1.py", 1)):
                p = subprocess.run([sys.executable, os.path.join(here, script), "--nift", fake,
                                    "--normal", normal, "--mutant", str(mutant)],
                                   stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=90)
                if p.returncode == 0 or "PASS" in p.stdout + p.stderr:
                    failures += 1
                    print(f"FAIL control {label}/{script}/M{mutant} passed without provenance")
    if failures:
        print(f"FAIL CP12C mutant controls: {failures} false positive(s)")
        return 1
    print("PASS CP12C mutant controls: fabricated stats, unrelated errors, timeouts, crashes and malformed stats are rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
