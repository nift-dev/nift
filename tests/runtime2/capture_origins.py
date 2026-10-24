#!/usr/bin/env python3
"""Required-VM loop-origin captures, compared with the actual legacy oracle.

Named selectors retain a source slot/path; held values share an iteration slot.
These cases distinguish source mutation, local reassignment, cached indexed
writes, callback body shape, frame lifetime, and invocation-local descriptors.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

cases={}
for origin in ['named','literal']:
 for aggregate in [False,True]:
  source='[[1]]' if aggregate else '[1]'
  value='x[0]' if aggregate else 'x'
  for body in ['expr','block']:
   fn=('()=>'+value) if body=='expr' else ('()=>{return '+value+'}')
   for action,statement in [('none',''),('assign','x='+('[7]' if aggregate else '7')+';'),('element','x[0]=9;' if aggregate else 'x+=1;'),('source','a[0]='+('[9]' if aggregate else '9')+';')]:
    if origin=='literal' and action=='source':continue
    for timing in ['before','after']:
     capture='f = '+fn+';'
     changes=capture+statement if timing=='before' else statement+capture
     src='f:=()=>0\n'+('a:='+source+'\n' if origin=='named' else '')+'for(x:'+('a' if origin=='named' else source)+'){'+changes+'}\nprint(f())\n'
     cases[f'{origin}_{"aggregate" if aggregate else "scalar"}_{body}_{action}_{timing}']=src

HERE = Path(__file__).resolve().parent
GOLDENS = {"capture_source_after": "10\n", "capture_named_aggregate": "1\n",
           "capture_source_repeated": "10\n10\n", "capture_named_repeated": "1\n1\n"}
for name in GOLDENS:
    cases[name] = (HERE / "cp12c-probes/src" / (name + ".f")).read_text()
cases.update({
    "frame_escape": "fn(make()){a:=[1];f:=()=>0;for(x:a){f = ()=>x;a[0]=9};return f}\nf:=make();print(f())\n",
    "parameter_escape": "fn(make(a)){f:=()=>0;for(x:a){f = ()=>x;a[0]=9};return f}\nf:=make([1]);print(f())\n",
    "descriptor_write_calls": "f:=()=>0\na:=[1]\nfor(x:a){f = ()=>{x+=1;return x}}\nprint(f());print(f())\n",
    "detached_slot_shared": "f:=()=>0\na:=[1]\nfor(x:a){x=7;f = ()=>x;x=8}\nprint(f())\n",
    "source_after_loop": "f:=()=>0\na:=[1,2]\nfor(x:a){f = ()=>x}\na[1]=9\nprint(f())\n",
    "source_rebound_after_loop": "f:=()=>0\na:=[1,2]\nfor(x:a){f = ()=>x}\na=[8,9]\nprint(f())\n",
    "nested_source": "f:=()=>0\na:=[[[1,2]],[[3,4]]]\nfor(x:a){for(y:x){for(z:y){f = ()=>z}}}\nprint(f())\n",
    "missing_after_loop": "f:=()=>0\na:=[1]\nfor(x:a){f = ()=>x}\na=[]\nprint(f())\n",
})

cases.update({'read_before': 'f:=()=>0\na:=[[1]]\nfor(x:a){print(x[0]);f = ()=>x[0];x[0]=9}\nprint(f())\n', 'callback_write': 'f:=()=>0\na:=[[1]]\nfor(x:a){f = ()=>{x[0]=9;return x[0]}}\nprint(f());print(f())\n', 'callback_read_write': 'f:=()=>0\na:=[[1]]\nfor(x:a){f = ()=>{print(x[0]);x[0]=9;return x[0]}}\nprint(f());print(f())\n', 'rebound_cached_owner': 'f:=()=>0\na:=[[1]]\nfor(x:a){print(x[0]);f = ()=>{x[0]=9;return x[0]}}\na=[[8]]\nprint(f())\n'})


MISSING_SELECTORS = {"nested_selector_missing_call", "nested_selector_missing_print", "selector_missing_index", "selector_missing_member",
                     "selector_missing_cached_write", "selector_missing_side_effect"}
ERROR_CASES = MISSING_SELECTORS | {"missing_after_loop"}
ERROR_CASES |= {"nested_source_view", "nested_decl_view", "nested_active_caller", "nested_third_escape"}
for name in MISSING_SELECTORS:
    cases[name] = (HERE / "cp12c-probes/src" / (name + ".f")).read_text()
GOLDENS["selector_missing_side_effect"] = "1\nafter-write\n"

cases.update({'nested_source_view': 'h:=()=>0\na:=[[1]]\nfor(x:a){h = ()=>()=>x[0]}\ng:=h()\na=[]\nprint(g())\n', 'nested_decl_view': 'h:=()=>0\na:=[[1]]\nfor(x:a){h = ()=>{f:=()=>x[0];return f}}\ng:=h()\na=[]\nprint(g())\n', 'nested_active_caller': 'h:=()=>0\na:=[[1]]\nfor(x:a){h = ()=>{return ()=>x[0]}}\ng:=h()\na=[]\nfn(invoke(cb)){cb()}\ninvoke(g)\n', 'nested_third_escape': 'h:=()=>0\na:=[[1]]\nfor(x:a){h = ()=>{return ()=>()=>x[0]}}\ng:=h()\ng=g()\na=[]\nprint(g())\n'})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--out")
    args = ap.parse_args()
    binary = str(Path(args.nift).resolve())
    failures, rows = 0, []
    with tempfile.TemporaryDirectory(prefix="capture-origins-") as temp:
        work = Path(temp)
        for name, source in cases.items():
            path = work / (name + ".f")
            path.write_text(source)
            results = {}
            for mode in ["0", "1", "require"]:
                stats = work / (name + "-" + mode + ".stats")
                env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": temp,
                       "LANG": "C", "LC_ALL": "C", "NIFT_PREVALIDATE": "off",
                       "NIFT_VM": mode, "NIFT_VM_STATS": str(stats)}
                p = subprocess.run([binary, str(path)], env=env, capture_output=True, timeout=15)
                telemetry = json.loads(stats.read_text().splitlines()[-1]) if stats.exists() else None
                result = (p.stdout, p.stderr.replace(temp.encode(), b"$ROOT"), p.returncode)
                results[mode] = result
                rows.append({"case": name, "mode": mode, "stdout": result[0].decode(),
                             "stderr": result[1].decode(), "exit": result[2], "stats": telemetry})
                if mode != "0" and (not telemetry or telemetry.get("vm_executions") != 1 or
                        telemetry.get("outcome") != ("vm-error" if name in ERROR_CASES else "vm")):
                    failures += 1
                    print(f"FAIL {name}/{mode}: required VM execution: {telemetry}")
            expected_exit = 1 if name in ERROR_CASES else 0
            if (results["0"][2] != expected_exit or results["0"] != results["1"] or
                    results["0"] != results["require"]):
                failures += 1
                print(f"FAIL {name}: {results}")
            elif name in GOLDENS and results["0"][0].decode() != GOLDENS[name]:
                failures += 1
                print(f"FAIL {name}: original regression golden changed")
            else:
                print(f"ok {name}")
    if args.out:
        Path(args.out).write_text(json.dumps(rows, indent=2) + "\n")
    print(f"{'FAIL' if failures else 'PASS'} capture origins: {len(cases)} cases, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
