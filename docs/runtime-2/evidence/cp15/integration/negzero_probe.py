#!/usr/bin/env python3
"""-0 / value-preservation probe: legacy vs NIFT_VM=require on one binary (CP15 integration, -0 defect; D-031 limits via $NIFT_LIM)."""
import json, os, subprocess, sys, tempfile
LIM = os.environ.get("NIFT_LIM", "")  # limits wrapper (D-031): <lim> 20 1048576 <cmd>; else timeout only
SHAPES = [
 ("arr-alone", "a := [-0]\nprint(a.stringify())\nprint(a[0])\n"),
 ("arr-nested", "a := [[-0], [1, [-0]]]\nprint(a.stringify())\n"),
 ("arr-in-obj-fallback", 'o := {"a": [-0], "c": .5}\nprint(o.stringify())\n'),
 ("scalar", "x := -0\nprint(x)\n"),
 ("scalar-concat", 'x := -0\nprint("v=" + x)\n'),
 ("scalar-stringify", "x := -0\nprint(x.stringify())\n"),
 ("fn-arg", "fn(f(v)) { return v }\nprint(f(-0))\n"),
 ("fn-arg-obj", 'fn(f(v)) { return v.b }\nprint(f({"b": -0}))\n'),
 ("fn-return", "fn(g()) { return -0 }\nprint(g())\n"),
 ("fn-return-obj", 'fn(g()) { return {"b": -0} }\nr := g()\nprint(r.stringify())\n'),
 ("arith-mul", "x := 0\ny := x * -1\nprint(y)\n"),
 ("arith-mul-dbl", "x := 0.5\ny := x * 0 * -1\nprint(y)\n"),
 ("arith-neg-dbl", "x := 0.5 - 0.5\ny := -x\nprint(y)\n"),
 ("arith-div", "x := -1.0\ny := x / 1e308 / 1e308\nprint(y)\n"),
 ("obj-var", 'o := {"b": -0}\np := o\nprint(p.b)\nprint(p.stringify())\n'),
 ("obj-var-member", 'o := {"b": -0}\nv := o.b\nprint(v)\nprint(v + 0)\nprint("s" + v)\n'),
 ("obj-print", 'o := {"b": -0}\nprint(o)\n'),
 ("obj-concat", 'o := {"b": -0}\nprint("o=" + o.stringify())\n'),
 ("obj-member-concat", 'o := {"b": -0}\nprint("b=" + o.b)\n'),
 ("obj-equal", 'o := {"b": -0}\nq := {"b": 0}\nprint(o == q)\nprint(o.b == 0)\n'),
 ("obj-loop", 'i := 0\nwhile (i < 2) { o := {"b": -0}\nprint(o.b)\ni += 1 }\n'),
 ("obj-loop-string", 'i := 0\nwhile (i < 2) { o := {"b": -0, "c": "s"}\nprint(o.b)\ni += 1 }\n'),
 ("obj-fnloop", 'fn(h()) { o := {"b": {"c": -0}}\nprint(o.stringify())\nreturn 0 }\ni := 0\nwhile (i < 1) { i += 1\nh() }\n'),
 ("obj-fntop", 'fn(h()) { o := {"b": {"c": -0}}\nprint(o.stringify())\nreturn 0 }\nh()\n'),
 ("nested-sort", 'a := [{"b": 1}, {"b": -0}]\nprint(a.stringify())\n'),
 ("nested-keys", 'o := {"a": {"b": [-0]}}\nprint(o.a.b[0])\nprint(o.a.stringify())\n'),
 ("json-negzero-arith", 'o := {"b": -0, "s": "x"}\nv := o.b\nprint(v + 0)\nprint(v * -1)\nprint(-v)\nprint(v == 0)\n'),
 ("json-negzero-double", 'o := {"e": -0.0}\nw := o.e\nprint(w)\nprint(w + 0)\nprint(-w)\nprint("s" + w)\n'),
 ("json-negzero-join", 'o := {"c": [-0, 1], "s": "x"}\nprint(o.c.join(","))\n'),
 ("div-json-negzero", 'o := {"b": -0, "s": "x"}\nprint(1 / o.b)\n'),
 ("arith-negzero-double", 'y := -0.5 * 0\nprint(y)\nprint("s" + y)\nprint(y == 0)\n'),
 ("big-1e400", 'o := {"a": 1e400}\nprint(o.stringify())\n'),
 ("denormal-min", 'o := {"a": 5e-324, "b": 4.9e-324, "c": 2.2250738585072014e-308}\nprint(o.stringify())\nprint(o.a)\n'),
 ("denormal-below", 'o := {"a": 1e-400, "b": -1e-400}\nprint(o.stringify())\n'),
 ("exact-int", 'o := {"a": 9007199254740993, "b": 12345678901234567890123}\nprint(o.stringify())\nprint(o.a)\n'),
 ("zero-frac", 'o := {"a": 0.0, "b": 1.0, "c": -0.0, "d": 1.50}\nprint(o.stringify())\nprint(o.a)\n'),
 ("exp-spell", 'o := {"a": 1E2, "b": 1e+2, "c": 2.5e-1, "d": 1e300, "e": 1e299}\nprint(o.stringify())\n'),
]
def run(nift, d, vm):
    env = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C", "HOME": d, "TMPDIR": d}
    st = os.path.join(d, "st")
    if os.path.exists(st): os.unlink(st)
    if vm: env.update(NIFT_VM="require", NIFT_VM_STATS=st)
    cmd = ([LIM, "20", "1048576"] if LIM else ["timeout", "-k", "5", "20"]) + [nift, "p.f"]
    p = subprocess.run(cmd, cwd=d, env=env, capture_output=True, text=True, timeout=60)
    s = json.loads(open(st).read().splitlines()[-1]) if vm and os.path.exists(st) else {}
    return (p.stdout, p.stderr.replace(d + "/", ""), p.returncode), s
def main():
    nift = os.path.abspath(sys.argv[1]); label = sys.argv[2] if len(sys.argv) > 2 else nift
    differ = refused = 0
    for name, src in SHAPES:
        d = tempfile.mkdtemp(dir=os.environ.get("TMPDIR"))
        open(os.path.join(d, "p.f"), "w").write(src)
        leg, _ = run(nift, d, False); vm, st = run(nift, d, True)
        out = st.get("outcome")
        if out not in ("vm", "vm-error"):
            refused += 1; verdict = f"refused ({st.get('outcome')} {st.get('reason')})"
        elif vm == leg: verdict = "same"
        else: differ += 1; verdict = "DIFFERS"
        print(f"[{label}] {name:20} {verdict}")
        if verdict == "DIFFERS":
            print(f"    legacy {leg[0]!r} rc={leg[2]} {leg[1].strip()!r}\n    vm     {vm[0]!r} rc={vm[2]} {vm[1].strip()!r}")
        subprocess.run(["rm", "-rf", d])
    print(f"[{label}] {len(SHAPES)} shapes: {differ} differ, {refused} refused (legacy runs them), {len(SHAPES)-differ-refused} same")
    return 1 if differ else 0
sys.exit(main())
