#!/usr/bin/env python3
"""Plain-data cycle routes are refused by the public operations (owner 2026-11-10 08:57, C;
combined-b3-b5.md section 67; D-NEXT-refsem #4; review of C67-C71 section 2).

The raw construction functions (build.h's Construction primitives, natives' builders) require
cycle-free input and do not check it; every script-reachable store goes through the VM's
mutation layer, which must refuse a store that would make a value contain itself, before any
change, with the catchable runtime.reference_cycle. Each route below must run on the VM (one VM
execution), print `runtime.reference_cycle` and leave the receiver's length unchanged. Legacy
copies instead (an approved difference); its output is pinned as an observation.

usage: cycle_refusal_routes.py --nift NIFT
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

# name -> (program, VM length after the refused store, legacy output)
ROUTES = {
    "push-self": ('a := [1]\ntry { a.push(a) } catch (e) { print(e.code) }\nprint(a.length())\n', 1, "2\n"),
    "index-store": ('a := [1, 2]\ntry { a[0] = a } catch (e) { print(e.code) }\nprint(a.length())\n', 2, "2\n"),
    "alias-index-store": ('a := [1, 2]\nb := a\ntry { b[1] = a } catch (e) { print(e.code) }\nprint(a.length())\n', 2, "2\n"),
    "through-object": ('a := [1]\ntry { a.push({"k": a}) } catch (e) { print(e.code) }\nprint(a.length())\n', 1, "2\n"),
    "object-inner": ('o := {"k": [1]}\ninner := o["k"]\ntry { inner.push(o) } catch (e) { print(e.code) }\n'
                     'print(inner.length())\n', 1, "2\n"),
    "mutual": ('a := [1]\nb := [2]\na.push(b)\ntry { b.push(a) } catch (e) { print(e.code) }\nprint(b.length())\n', 1, "2\n"),
    "insert": ('a := [1]\ntry { a.insert(0, a) } catch (e) { print(e.code) }\nprint(a.length())\n', 1, "2\n"),
    "splice": ('a := [1]\nb := [a]\ntry { a.splice(0, 0, [b]) } catch (e) { print(e.code) }\nprint(a.length())\n', 1, "2\n"),
    "nested-literal": ('a := [1]\ntry { a.push([a, [a]]) } catch (e) { print(e.code) }\nprint(a.length())\n', 1, "2\n"),
    "function-parameter": ('fn(f(xs)) { try { xs.push(xs) } catch (e) { print(e.code) }\nreturn xs.length() }\n'
                           'a := [1]\nprint(f(a))\n', 1, "2\n"),
    "closure-captured": ('a := [1]\ng := () => a.push(a)\ntry { g() } catch (e) { print(e.code) }\nprint(a.length())\n',
                         1, "2\n"),
    # Review of C72-C78, T-1: values built by a clone carry the source's cycle summary
    # (copy_summary); a clone that dropped it would let the store below close a real cycle.
    # object_shallow_clone: merge (C76) and every merge_deep level.
    "merge-result": ('arr := [1]\no := {"a": arr}\nm := o.merge({"b": 2})\ntry { arr.push(m) } catch (e) { print(e.code) }\n'
                     'print(arr.length())\n', 1, "2\n"),
    "merge-deep-result": ('arr := [1]\no := {"a": arr}\nm := o.merge_deep({"b": 2})\ntry { arr.push(m) } catch (e) '
                          '{ print(e.code) }\nprint(arr.length())\n', 1, "2\n"),
    # collection_shallow_clone: copy()/deepcopy() of a map or stack (copy_graph).
    "copy-map": ('arr := [1]\nm := map()\nm.set("a", arr)\nc := copy(m)\ntry { arr.push(c) } catch (e) { print(e.code) }\n'
                 'print(arr.length())\n', 1, "2\n"),
    "deepcopy-map": ('arr := [1]\nm := map()\nm.set("a", arr)\nc := deepcopy(m)\nx := c.get("a")\ntry { x.push(c) } '
                     'catch (e) { print(e.code) }\nprint(x.length())\n', 1, "2\n"),
    "copy-stack": ('arr := [1]\ns := stack()\ns.push(arr)\nc := copy(s)\ntry { arr.push(c) } catch (e) { print(e.code) }\n'
                   'print(arr.length())\n', 1, "2\n"),
    # copy() of an object and an array (copy_graph's shallow copy).
    "copy-object": ('arr := [1]\no := {"a": arr}\nc := copy(o)\ntry { arr.push(c) } catch (e) { print(e.code) }\n'
                    'print(arr.length())\n', 1, "2\n"),
    # Review of C79-C82, C79-L2: deepcopy() of an array and of an object fills the copies through
    # Construction::array_push / object_set (copy_graph), whose per-value marking (note_store) the
    # deepcopy-map route does not reach: the copy's inner array must know it is held by the copy.
    "deepcopy-array": ('arr := [[1]]\nc := deepcopy(arr)\nx := c[0]\ntry { x.push(c) } catch (e) { print(e.code) }\n'
                       'print(x.length())\n', 1, "2\n"),
    "deepcopy-object": ('o := {"k": [1]}\nc := deepcopy(o)\nx := c["k"]\ntry { x.push(c) } catch (e) { print(e.code) }\n'
                        'print(x.length())\n', 1, "2\n"),
    "copy-array": ('arr := [1]\nouter := [arr]\nc := copy(outer)\ntry { arr.push(c) } catch (e) { print(e.code) }\n'
                   'print(arr.length())\n', 1, "2\n"),
    # array_shallow_clone is only an internal snapshot (for-loop source, sort keys and source): the
    # loop snapshot cannot be stored, the array it was taken from can.
    "loop-snapshot": ('arr := [1]\nouter := [arr]\nfor (x : outer) { try { arr.push(outer) } catch (e) { print(e.code) } }\n'
                      'print(arr.length())\n', 1, "2\n"),
}


def run(nift, source, mode, work):
    with open(os.path.join(work, "p.f"), "w") as handle:
        handle.write(source)
    stats = os.path.join(work, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": work, "LANG": "C", "NIFT_VM": mode,
           "NIFT_VM_STATS": stats}
    p = subprocess.run([nift, "p.f"], cwd=work, env=env, capture_output=True, text=True, timeout=60)
    st = None
    if os.path.exists(stats):
        with open(stats) as handle:
            rows = [json.loads(line) for line in handle if line.strip()]
        st = rows[-1] if rows else None
    return p.returncode, p.stdout, p.stderr, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    nift = os.path.abspath(ap.parse_args().nift)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="cycle-routes-") as work:
        for name, (source, length, legacy_out) in ROUTES.items():
            rc, out, err, st = run(nift, source, "1", work)
            ok = (rc == 0 and out == "runtime.reference_cycle\n%d\n" % length and st is not None
                  and st.get("outcome") == "vm" and st.get("vm_executions") == 1 and st.get("legacy_executions") == 0)
            lrc, lout, lerr, _ = run(nift, source, "0", work)
            lok = lrc == 0 and lout == legacy_out
            print("%s %-20s VM refused before the store, length %d; legacy copies (%s)"
                  % ("PASS" if ok and lok else "FAIL", name, length, "pinned" if lok else "CHANGED: %r" % lout))
            if not ok:
                print("  VM: exit %d stdout %r stderr %r outcome %s" % (rc, out, err, st and st.get("outcome")))
            failures += 0 if ok and lok else 1
    print("%s cycle refusal routes: %d route(s), %d failure(s)" % ("PASS" if not failures else "FAIL", len(ROUTES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
