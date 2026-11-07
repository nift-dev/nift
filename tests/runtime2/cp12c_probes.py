#!/usr/bin/env python3
"""Run each probe under legacy (NIFT_VM=0), NIFT_VM=1 and NIFT_VM=require.

run_probes.py NIFT SRC_DIR OUT_DIR [extra_env=val ...]
Writes OUT_DIR/<name>.<mode>.{out,err,exit,stats} and OUT_DIR/table.md.
"""
import json, os, shutil, subprocess, sys, tempfile

# B5 fix-up 2 (docs/runtime-2/checkpoints/b5-refsem.md 12.2): the CP12C probes were written for
# legacy's live loop (the loop variable is a location of the source element). B5 iterates the
# entry snapshot with an ordinary per-iteration binding, so 48 probes in src/ differ from legacy.
# Each was verified individually and is pinned here with its approved rule (kept distinct):
#   snapshot: snapshot iteration (owner 2026-11-01 "loops iterate a snapshot taken at entry",
#             2026-11-02 "snapshot iteration kept"; plan section 3 Iteration, D5; Q3 rows 5-6).
#   D1:       per-iteration loop-variable binding (owner 2026-11-07 06:55 decision 1).
# The original expectation (legacy-equal) and each legacy / 17f7964c / B5 result are preserved in
# docs/runtime-2/evidence/b5/cp12c-probes-original.json. A pinned probe must give exactly its pinned
# stdout, no stderr and exit 0 under NIFT_VM=1 and NIFT_VM=require, and must still differ from
# legacy (if it becomes legacy-equal the pin is stale and the probe FAILS). Every other probe must
# be legacy-equal, as before.
B5_APPROVED = {
    'capture_live_at_creation': ('1\n', 'snapshot', 'D5 live reads'),
    'capture_mixed_live': ('false\n', 'snapshot', 'D5 live reads'),
    'capture_named_aggregate': ('9\n', 'snapshot', 'D5 element mutation'),
    'capture_named_repeated': ('9\n9\n', 'snapshot', 'D5 element mutation'),
    'capture_nested_live': ('1\n', 'snapshot', 'D5 live reads'),
    'capture_source_after': ('1\n', 'snapshot', 'D5 live reads'),
    'capture_source_repeated': ('1\n1\n', 'snapshot', 'D5 live reads'),
    'captured_compound_write': ('6\n7\n', 'snapshot', 'D5 live reads'),
    'captured_postfix_inc': ('2\n3\n', 'snapshot', 'D5 live reads'),
    'captured_prefix_inc': ('2\n3\n', 'snapshot', 'D5 live reads'),
    'eager_prefix_statement': ('9\n8\n13\n12\n', 'snapshot', 'D5 live reads'),
    'escape_before_assign': ('7\n7\n7\n', 'D1', 'D1 loop-variable closures'),
    'f01_global_fn_before': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f02_global_fn_after_loop': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f03_late_capture_in_while': ('1\n2\n3\n1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f04_closure_param_mut': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f05b_fn_local_captured': ('1\n2\n3\n0\n', 'snapshot', 'D5 nothing vanishes'),
    'f08_alias_mutate': ('1\n2\n3\n77\n', 'snapshot', 'D5 live reads'),
    'f11_slice_reassign': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f12_global_from_fn_loop': ('1\n2\n3\n0\n', 'snapshot', 'D5 nothing vanishes'),
    'f13_global_from_fn_loop_elem': ('1\n2\n3\n0\n', 'snapshot', 'D5 live reads'),
    'f14_nested_same': ('1\n2\n3\n1\n9\n2\n9\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f17_obj_global_fn': ('a\n1\nb\n2\nc\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f20_while_inside_shrinks': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f22_toplevel_lambda_obj': ('a\n1\nb\n2\nc\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'f24_global_elem_write_via_fn': ('1\n2\n3\ndone\n', 'snapshot', 'D5 live reads'),
    'g02_obj_global_fn_same_keys': ('1\n2\ndone\n', 'snapshot', 'D5 live reads'),
    'g03_arr_global_fn_same_len': ('1\n2\ndone\n', 'snapshot', 'D5 live reads'),
    'm10_unvisited_change': ('1\n2\n3\n', 'snapshot', 'D5 live reads'),
    'nested_selector_missing_call': ('', 'snapshot', 'D5 live reads'),
    'nested_selector_missing_print': ('1\n', 'snapshot', 'D5 live reads'),
    'prefix_dec_live_source': ('8\n8\n8\n8\n12\n12\n12\n12\n', 'snapshot', 'D5 live reads'),
    'prefix_live_source': ('8\n8\n8\n8\n12\n12\n12\n12\n', 'snapshot', 'D5 live reads'),
    'selector_missing_cached_write': ('1\n9\n', 'snapshot', 'D5 element mutation'),
    'selector_missing_index': ('1\n', 'snapshot', 'D5 live reads'),
    'selector_missing_member': ('1\n', 'snapshot', 'D5 live reads'),
    'selector_missing_side_effect': ('1\nafter-write\n9\n', 'snapshot', 'D5 element mutation'),
    't06_fn_global': ('s\n1\ns\n2\ns\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    't08_closure_captured': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    't09_pop_current': ('1\n2\n3\ndone\n', 'snapshot', 'D5 live reads'),
    't10_pop_later': ('1\n2\n3\n4\ndone\n', 'snapshot', 'D5 live reads'),
    't14_obj_replace_values': ('a\n1\nb\n2\nc\n3\ndone\n', 'snapshot', 'D5 live reads'),
    't16_same_iter_modify': ('1\n2\n3\n99\n', 'snapshot', 'D5 live reads'),
    't17_restored_before_read': ('1\n2\n3\ndone\n', 'snapshot', 'D5 live reads'),
    't24_nested_same_source': ('1\n2\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    't26_read_after_shrink_first_iter': ('1\n2\n3\n', 'snapshot', 'D5 nothing vanishes'),
    't29_nested_taken_read': ('s\n1\ns\n2\ns\n3\ndone\n', 'snapshot', 'D5 nothing vanishes'),
    'write_before_live_capture': ('1\n2\n', 'snapshot', 'D5 live reads'),
}
nift, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
extra = dict(a.split("=", 1) for a in sys.argv[4:])
os.makedirs(out, exist_ok=True)
modes = [("legacy", "0"), ("vm1", "1"), ("req", "require")]
rows = []
for f in sorted(os.listdir(src)):
    if not f.endswith(".f"):
        continue
    name = f[:-2]
    res = {}
    for label, mode in modes:
        d = tempfile.mkdtemp(prefix="p-")
        shutil.copy(os.path.join(src, f), os.path.join(d, "p.f"))
        mod = os.path.join(src, name + ".m.nft")
        if os.path.exists(mod):
            shutil.copy(mod, os.path.join(d, "m.nft"))
        stats = os.path.join(d, "stats.json")
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", "LC_ALL": "C",
               "NIFT_PREVALIDATE": "off", "NIFT_VM": mode, "NIFT_VM_STATS": stats}
        env.update(extra)
        try:
            p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, timeout=60,
                               stdin=subprocess.DEVNULL)
            o, e, rc = p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace"), p.returncode
        except subprocess.TimeoutExpired:
            o, e, rc = "", "TIMEOUT", 124
        e = e.replace(d, "$R")
        st = ""
        if os.path.exists(stats):
            try:
                j = json.loads(open(stats).read().splitlines()[-1])
                st = f"{j.get('outcome')}" + (f"/{j.get('reason')}" if j.get('reason') else "")
                if "heap_peak" in j:
                    st += f" peak={j.get('heap_peak')} live={j.get('heap_live')}"
            except Exception as ex:  # noqa
                st = f"badstats {ex}"
        for ext, val in (("out", o), ("err", e), ("exit", str(rc)), ("stats", st)):
            open(os.path.join(out, f"{name}.{label}.{ext}"), "w").write(val)
        res[label] = (o, e, rc, st)
        shutil.rmtree(d, ignore_errors=True)
    rows.append((name, res))

def cell(r):
    o, e, rc, st = r
    s = o.strip().replace("\n", " / ")
    if e.strip():
        s += " ⟨err: " + e.strip().replace("\n", " / ") + "⟩"
    return f"{s} [exit {rc}]".replace("|", "\\|")

with open(os.path.join(out, "table.md"), "w") as t:
    t.write("| probe | legacy (NIFT_VM=0) | NIFT_VM=1 | VM=1 stats | require | require stats | same(1) |\n")
    t.write("|---|---|---|---|---|---|---|\n")
    for name, r in rows:
        same = (r["legacy"][:3] == r["vm1"][:3])
        t.write(f"| {name} | {cell(r['legacy'])} | {cell(r['vm1'])} | {r['vm1'][3]} | {cell(r['req'])} | {r['req'][3]} | {'yes' if same else '**NO**'} |\n")
print(open(os.path.join(out, "table.md")).read())
bad = 0
pinned = 0
for name, r in rows:
    if name in B5_APPROVED:
        pinned += 1
        stdout, rule, sub = B5_APPROVED[name]
        for label in ("vm1", "req"):
            if r[label][:3] != (stdout, "", 0):
                print(f"FAIL probe {name}: {label} is not the pinned B5 result ({rule}: {sub})"); bad += 1
        if r["legacy"][:3] == r["vm1"][:3]:
            print(f"FAIL probe {name}: pinned as a B5 difference ({rule}) but now legacy-equal: stale pin"); bad += 1
    elif r["legacy"][:3] != r["vm1"][:3]:
        print(f"FAIL probe {name}: legacy and VM differ"); bad += 1
    if "internal-error" in r["vm1"][3] or "no-stats" in r["vm1"][3]:
        print(f"FAIL probe {name}: {r['vm1'][3]}"); bad += 1
missing = [n for n in B5_APPROVED if n not in dict(rows)]
if os.path.basename(os.path.normpath(src)) == "src" and missing:
    print(f"FAIL pinned probes missing from {src}: {missing}"); bad += 1
if bad:
    sys.exit(1)
print(f"PASS CP12C probes: {len(rows)} cases ({pinned} pinned B5 differences, the rest legacy/VM equivalent), no internal failures")
