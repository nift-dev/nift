#!/usr/bin/env python3
"""CP12 phase B coverage inventory: every refusal reason of every unit.

  cp12b_inventory.py collect --nift ./nift --out DIR [--projunits DIR] [--gen 300] [--seed 1]
  cp12b_inventory.py analyse DIR [--json OUT]
  cp12b_inventory.py owners DIR       the ownership table (D-036), receiver-aware
  cp12b_inventory.py selftest

collect runs `NIFT_VM=classify-all` (parse + compile only, nothing executes;
src/rt/refusal_inventory.h) over
  shared     the shared corpora exactly as `test-rt-vm`'s rt-diff step sees
             them (same corpora, --extract-smoke tests, rt-diff --gen 200
             --seed S), statistics per entry (rt-diff --b-vm-stats); both
             sides run classify-all, so rt-diff's verdict is meaningless here
  projects   the real-project units of the CP13 zero-false-rejection gate
             (units.tsv from scratchpad cp13/projtool/extract.py), if given
  generator  the general generator (tests/runtime2/vm_gen.py program(seed, i))
and writes DIR/<corpus>.jsonl, one statistics line per script (the
NIFT_VM_STATS line plus `corpus` and `id`).

analyse reads them and prints (and with --json writes) the distributions the
CP12b inventory reports: reasons by scripts blocked, sole blockers, number of
distinct reasons per script, single-feature unlocks, the greedy climb, and the
checkpoint of every reason. A script counts as unlocked by a feature set S
when its inventory is complete and every non-consequential reason is in S
("certain"); "strict" also requires the consequential ones.
"""
import argparse
import collections
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tests", "runtime2"))

SHARED = ["tests/runtime2/vm-corpus", "tests/runtime2/semantics", "tests/runtime2/rt-diff-corpus",
          "tests/conformance/cases", "docs/evidence/cp410-competitiveness/probes"]


def wrapper(nift, work):
    path = os.path.join(work, "nift-classify-all")
    with open(path, "w") as f:
        f.write("#!/bin/sh\nNIFT_VM=classify-all\nexport NIFT_VM\nexec %s \"$@\"\n" % nift)
    os.chmod(path, 0o755)
    return path


def collect_shared(nift, out, seed, work):
    w = wrapper(nift, work)
    report = os.path.join(work, "rt-diff-report.json")
    cmd = [sys.executable, "tools/rt-diff/rt-diff", "--a", w, "--b", w, "--no-trace", "--jobs", "1",
           "--extract-smoke", "tests", "--gen", "200", "--seed", str(seed), "--quiet", "--b-vm-stats",
           "--report", report, "--timeout", "60"]
    for c in SHARED:
        cmd += ["--corpus", c]
    subprocess.run(cmd, cwd=REPO, stdout=subprocess.DEVNULL)
    seen = {}
    for r in json.load(open(report))["results"]:
        for st in r.get("vm_stats") or []:
            key = r["id"] + "|" + os.path.basename(st["path"])
            st.update(corpus="shared", id=key)
            seen[key] = st
    with open(os.path.join(out, "shared.jsonl"), "w") as f:
        for key in sorted(seen):
            f.write(json.dumps(seen[key], sort_keys=True) + "\n")
    return len(seen)


def classify_file(nift, path, work):
    stats = os.path.join(work, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": "/nonexistent", "NIFT_VM": "classify-all",
           "NIFT_VM_STATS": stats, "LANG": "C"}
    subprocess.run(["timeout", "60", nift, os.path.basename(path)], cwd=os.path.dirname(path), env=env,
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        return json.loads(open(stats).read().splitlines()[-1])
    except (OSError, IndexError, ValueError):
        return {"outcome": "no-stats", "reason": "no-stats", "reasons": [], "inventory_complete": False,
                "inventory_stop": "no-stats"}


def collect_projects(nift, out, units, work):
    rows = 0
    with open(os.path.join(out, "projects.jsonl"), "w") as f:
        for line in open(os.path.join(units, "units.tsv")):
            if line.startswith("#") or not line.strip():
                continue
            repo, _url, _commit, origin, fixture, kind, _sha = line.rstrip("\n").split("\t")
            st = classify_file(nift, os.path.join(units, fixture), work)
            st.update(corpus="projects", id=fixture, unit_kind=kind, origin=repo + ":" + origin)
            f.write(json.dumps(st, sort_keys=True) + "\n")
            rows += 1
    return rows


def collect_generator(nift, out, count, seed, work):
    import vm_gen
    with open(os.path.join(out, "generator.jsonl"), "w") as f:
        for i in range(count):
            path = os.path.join(work, "gen", "gen.f")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as g:
                g.write(vm_gen.program(seed, i))
            st = classify_file(nift, path, work)
            st.update(corpus="generator", id="seed%d/%d" % (seed, i))
            f.write(json.dumps(st, sort_keys=True) + "\n")
    return count


# ---- analysis -------------------------------------------------------------

def load_lists():
    """CP14/CP15 built-in names: the repository's own X-macro list (CP15 is
    integrated; entries after the first `---- CP15` banner are CP15's), else
    the CP14 integration and CP15 branch checkouts beside it; else empty."""
    names = {"CP14": set(), "CP15": set()}
    own = os.path.join(REPO, "src", "rt", "natives", "builtin_list.inc")
    if os.path.exists(own):
        cp = "CP14"
        for line in open(own):
            if line.startswith("// ----") and "CP15" in line:
                cp = "CP15"
            if line.startswith("NIFT_RT_BUILTIN("):
                names[cp].add(line.split('"')[1])
        return names
    base = os.path.dirname(REPO)
    for cp, path in (("CP14", "nift-integ/src/rt/natives/builtin_list.inc"),
                     ("CP15", "nift-cp15/src/rt/natives/builtin_list.inc")):
        try:
            for line in open(os.path.join(base, path)):
                if line.startswith("NIFT_RT_BUILTIN("):
                    names[cp].add(line.split('"')[1])
        except OSError:
            pass
    names["CP15"] -= names["CP14"]
    return names


HOST = {"read", "write", "exists", "is_file", "is_dir", "ls", "run", "cd", "mkdir", "make_dir", "rm", "cp", "mv",
        "copy", "move", "remove", "getenv", "setenv", "unsetenv", "env", "stat", "file", "open", "touch", "sleep",
        "pwd", "which", "read_all", "read_line", "write_line", "read_bytes", "read_all_bytes", "cat", "system",
        "exec", "shell", "input", "time", "epoch", "timer", "date", "platform", "os", "arch", "glob", "path",
        "append", "ofstream", "ifstream", "save", "status", "wait", "thread", "jobs", "hardware_concurrency"}
TEMPLATE = {"content", "title", "page", "render", "inject", "section", "project", "build", "build_all", "minify",
            "highlight", "html", "css", "js", "jsx", "svg", "xml", "script", "track", "untrack", "tracked"}


def legacy_names():
    """The legacy evaluator's special names (src/rt/legacy_names.cpp)."""
    text = open(os.path.join(REPO, "src", "rt", "legacy_names.cpp")).read()
    body = text[text.index("kNames[] = {"):text.index("};", text.index("kNames[] = {"))]
    return set(part.strip().strip('"') for part in body.split("{", 1)[1].split(",") if '"' in part)


LEGACY = legacy_names()
STRUCTISH = ("statement:StructDecl", "opaque:struct-body")
LAMBDA_METHODS = {"map", "filter", "sort", "sort_by", "group_by", "reduce", "find", "any", "all", "each", "count_by"}


def checkpoint(reason, lists, ctx=frozenset()):
    """(checkpoint, scope) for one refusal reason; `ctx` is every reason of
    the same script (struct methods and module functions are recognised by
    the script also declaring structs or importing)."""
    head, _, name = reason.partition(":")
    if reason == "opaque:irregular-legacy-parse":
        return "language-cleanup", "irregular legacy parse"
    if reason in ("opaque:template-text", "opaque:deferred-translation", "expression-statement"):
        return "CP24", "template-only construct"
    if reason in STRUCTISH:
        return "CP12b", "structs"
    if reason == "statement:EnumDecl":
        return "CP12b", "enums"
    if reason in ("statement:Import", "statement:Export"):
        return "CP12b", "modules/imports"
    if reason in ("unresolved-name", "free-name:undeclared", "assign-free-name:undeclared") and \
            "statement:Import" in ctx:
        return "CP12b", "modules/imports (imported binding)"
    if head in ("free-name", "assign-free-name"):
        return "CP12b", "globals" + ("" if name in ("", "global") else " (refused: %s)" % name)
    if reason == "unresolved-name:legacy-binding":
        return "CP16/CP24", "legacy script/template bindings (args, env, page...)"
    if reason == "unresolved-name":
        return "CP12b", "GETDYN/dynamic lookup (top-level name not declared)"
    if reason in ("fn-after-statement", "fn-called-before-declaration", "fn-redeclared", "binding-shadows-fn", "param-shadows-fn",
                  "statement:FnDecl"):
        return "CP12b", "resolver: function declarations"
    if reason in ("fn-as-value", "call-binding", "expression:Lambda", "callee"):
        return "unassigned", "first-class functions / lambdas"
    if reason in ("statement:Try", "statement:Throw") or head == "static-error":
        return "CP13", "errors / try-catch"
    if reason == "statement:Command":
        return "CP19", "shell"
    if reason == "statement:TemplateStmt":
        return "CP24", "template-only construct"
    if reason == "statement:For":
        return "unassigned", "for loops"
    if reason == "fn-flags":
        return "CP22/CP24", "async / @fn / struct methods"
    if reason == "expression:Await":
        return "CP22", "async / await"
    if head == "opaque":
        return "language-cleanup", "legacy run-time error construct (" + name + ")"
    if head in ("callable", "method"):
        for cp in ("CP14", "CP15"):
            if name in lists[cp]:
                return cp, "built-ins"
        if name in HOST:
            return "CP16", "host ops"
        if name in TEMPLATE:
            return "CP24", "template functions"
        if head == "method" and name in LAMBDA_METHODS:
            return "unassigned", "lambda-taking collection methods (not in CP14/CP15)"
        if head == "method" and ctx & set(STRUCTISH):
            return "CP12b", "structs (method of a user struct)"
        if name in LEGACY or "<" in name:
            return "unassigned", "legacy built-in not scheduled (map/set/mutex/ffi/cmd...)"
        if head == "callable" and ctx & {"statement:Import"}:
            return "CP12b", "modules/imports (imported function)"
        if head == "callable":
            return "CP12b", "unknown callable (dynamic or module function)"
        return "unassigned", "method not in CP14/CP15"
    if head in ("type", "context") or reason in ("aliasing", "alias-argument", "path-in-call-argument",
                                                 "path-in-index", "prepared-unpreparable", "prepared-fn-unprepared",
                                                 "prepared-decl-type", "param-type-unpredicted",
                                                 "push-receiver"):
        return "VM subset (D-018/kinds)", "compiler precision"
    if reason == "postfix-receiver":
        return "VM subset", "chained postfix on a call result (a.b().c(), f().k)"
    if reason == "postfix-receiver:member":
        return "language-cleanup", "member read on a call result or literal (L43)"
    if head in ("binding-name-reserved", "fn-name-reserved", "param-name-reserved"):
        return "language-cleanup", "names legacy reserves (L6, D-009#3)"
    if reason in ("jump-not-last", "shebang-statement"):
        return "language-cleanup", "D-014 / statement quirks"
    if reason == "parse-error":
        return "none", "parse error (legacy rejects too)"
    if reason in ("nesting-limit", "too-many-functions") or head in ("limit", "analysis-limit"):
        return "limits", "analysis limits"
    return "VM subset", "expression/statement subset (" + reason + ")"


# ---- owners (D-036: every refusal reason has a named checkpoint or phase) ----

CONCURRENCY = {"mutex", "thread", "lock", "unlock", "try_lock", "locked", "store", "fetch_add", "load", "done",
               "start", "pause", "resume", "stop", "paused", "start_private_async", "await"}
PROCESS = {"cmd", "pipe"}
HOST_MORE = {"close", "cwd", "elapsed", "open_bytes", "sha256_file", "project_root", "write_document", "save",
             "write", "open"}
ERRORS = {"error", "err", "warn", "throw", "try", "catch", "exit", "nope", "undefined_fn", "undefined_thing"}
SUBSET_FORMS = {"declare-const", "declare-pattern", "assign-target", "compound-target", "index-target",
                "index-assign-target", "incdec-target", "expression:IncDec", "spread", "fn-param", "jump-outside-loop",
                "string-arg-type", "postfix-spelling", "text-type-name", "trailing-comma", "object-key",
                "object-duplicate-key", "comparison-chain", "prefix-not-precedence", "binary-operator",
                "unary-operator", "logical-operator", "compound-operator", "expression-nesting", "method-arity",
                "push-receiver", "postfix-receiver-call", "statement:FnDecl"}


# Constructs whose refusal leaves a receiver the VM cannot type: a method
# called on what they produce is re-reported as `method:<name>` (directly,
# when chain masking replaces the refused call, or through the binding that
# holds the result). Coordinator, 2026-10-20; decisions.md "CP15B scope
# correction" (runtime-2 9162c477).
HANDLE_CONSTRUCTORS = {"map", "set", "stack", "queue", "file", "open", "ifstream", "ofstream", "open_bytes",
                       "read_bytes", "bytes", "stream", "timer", "collection"}


def has_native(name, lists):
    return any(name in names for names in lists.values())


def receiver_constructs(lists, ctx):
    """The script's refused constructs that can be a method's receiver."""
    out = []
    for r in sorted(ctx):
        head, _, name = r.partition(":")
        if r in STRUCTISH:
            out.append(r)
        elif head == "callable" and not has_native(name, lists) and (
                name in CONCURRENCY or name in PROCESS or name in HANDLE_CONSTRUCTORS or name.startswith("ffi_")
                or name.startswith("atomic")):
            out.append(r)
    return out


def receiver_owner(name, lists, ctx, item):
    """Owner of `method:<name>` when a native for `name` exists for some
    receiver kind: the refused receiver construct's owner; else (no refused
    receiver in the script) the inference of receiver kinds; ambiguous or
    unidentified receivers are reported as unattributed, never guessed."""
    candidates = receiver_constructs(lists, ctx)
    owners = sorted({owner(c, lists, ctx)[0] for c in candidates})
    if len(owners) == 1:
        return owners[0], "method on a refused receiver (" + ", ".join(candidates) + ")"
    if len(owners) > 1:
        return "unattributed", "method with a native; refused receivers of several owners (" + ", ".join(owners) + ")"
    if item is not None and item.get("consequential"):
        return "unattributed", "method with a native on a masked receiver (receiver not identified)"
    return "CP12C", "receiver kinds (inference)"


def owner(reason, lists, ctx=frozenset(), item=None):
    """(owning checkpoint or phase, scope) for every refusal reason; `item`
    is the inventory entry ({reason, consequential, ...}) when known."""
    head, _, name = reason.partition(":")
    if reason in ("fn-after-statement", "fn-called-before-declaration"):
        return "CP12b", "B1 declaration order (done)"
    if reason == "postfix-receiver":
        return "CP12b", "chained postfix (this phase)"
    if reason == "postfix-receiver:member":
        return "language-cleanup", "L43 member-read message on call results"
    if head in ("free-name", "assign-free-name") and name in ("dynamic-caller", "dynamic-shadow"):
        return "language-cleanup", "L25 lexical scoping (refused; migration diagnostics)"
    importing = "statement:Import" in ctx
    if head in ("free-name", "assign-free-name", "unresolved-name"):
        if name == "legacy-binding":
            return "CP16/CP24", "legacy script/template bindings (args, env, page)"
        if importing and name in ("", "undeclared", "unanalysed"):
            return "CP12b", "modules (imported binding)"
        if name in ("global", "unanalysed"):
            return "CP12b", "globals"
        return "CP13", "undefined name (legacy run-time error)"
    if reason in ("fn-as-value", "call-binding", "expression:Lambda", "callee", "statement:For", "statement:FnDecl"):
        return "CP12C", "functions as values, closures, lambdas, for"
    if head == "method" and (has_native(name, lists) or name == "push"):
        return receiver_owner(name, lists, ctx, item)
    if head in ("callable", "method"):
        for cp in ("CP14", "CP15"):
            if name in lists[cp]:
                return cp, "built-ins"
        if head == "method" and name in LAMBDA_METHODS:
            return "CP14B", "callback-taking collection methods"
        if importing and name not in LEGACY:
            return "CP12b", "modules (imported function or module member)"
        if head == "method" and ctx & set(STRUCTISH):
            return "CP12b", "structs (method of a user struct)"
        if name.startswith("ffi_"):
            return "CP18", "FFI"
        if name in CONCURRENCY or name.startswith("atomic"):
            return "CP22/CP23", "concurrency (threads, mutexes, atomics)"
        if name in PROCESS:
            return "CP16/CP20", "processes and pipelines"
        if name in HOST or name in HOST_MORE:
            return "CP16", "host ops (files, environment, time)"
        if name in TEMPLATE:
            return "CP24", "template functions"
        if name in ERRORS:
            return "CP13", "errors (undefined callable, error/warn/throw)"
        if name == "push":
            return "CP14", "push as an expression (records20k, integration owner)"
        if head == "callable" and name not in LEGACY:
            # A unit's own name whose declaration is refused: its construct owns it.
            if ctx & set(STRUCTISH):
                return "CP12b", "structs (constructor call)"
            if "fn-flags" in ctx:
                return "CP22/CP24", "fn[async] (CP22), @fn deferred bodies (CP24)"
            return "CP13", "errors (undefined callable, error/warn/throw)"
        if head == "method":
            return "unattributed", "method without a native and no identified owner"
        return "CP15B", "remaining pure built-ins and methods"
    if reason in ("statement:Try", "statement:Throw", "parse-error") or head in ("static-error", "type"):
        return "CP13", "errors, try/catch, type errors with legacy diagnostics, prevalidation"
    if head == "context" or head.startswith("prepared"):
        return "language-cleanup", "D-018 loop-evaluator differences (L1-L4)"
    if reason in ("aliasing", "alias-argument", "path-in-call-argument", "path-in-index", "param-type-unpredicted"):
        return "language-cleanup", "L7 aliasing / location references (records20k aliasing: CP14 integration)"
    if reason in STRUCTISH or reason == "statement:EnumDecl":
        return "CP12b", "structs" if reason in STRUCTISH else "enums (B6 deferred)"
    if reason in ("statement:Import", "statement:Export"):
        return "CP12b", "modules"
    if reason == "fn-flags":
        return "CP22/CP24", "fn[async] (CP22), @fn deferred bodies (CP24)"
    if reason == "expression:Await":
        return "CP22", "await"
    if reason == "statement:Command":
        return "CP19", "shell"
    if reason in ("opaque:template-text", "opaque:deferred-translation", "expression-statement",
                  "statement:TemplateStmt"):
        return "CP24", "template-only constructs"
    if head == "opaque" or reason in ("jump-not-last", "shebang-statement"):
        return "language-cleanup", "legacy parse/run-time quirks (D-014, D-022)"
    if head in ("binding-name-reserved", "fn-name-reserved", "param-name-reserved"):
        return "language-cleanup", "L6 names legacy reserves"
    if reason in ("interpolation", "string-literal", "number-literal"):
        return "CP15B", "string and number literal spellings, interpolation"
    if reason in SUBSET_FORMS:
        return "CP12C", "statement and binding forms (const, destructuring, element targets, spread)"
    if reason in ("nesting-limit", "too-many-functions") or head in ("limit", "analysis-limit"):
        return "CP12C", "compiler limits"
    return "CP12C", "other statement/expression forms (" + reason + ")"


def script_sets(rows):
    """Per script: (complete, certain reasons, all reasons)."""
    out = []
    for st in rows:
        if st.get("outcome") in ("compiled", "vm", "vm-error"):
            out.append((st, True, frozenset(), frozenset()))
            continue
        reasons = st.get("reasons") or [{"reason": st.get("reason", "?"), "consequential": False}]
        certain = frozenset(r["reason"] for r in reasons if not r["consequential"])
        every = frozenset(r["reason"] for r in reasons)
        out.append((st, bool(st.get("inventory_complete")), certain, every))
    return out


def greedy(sets, limit=20):
    """Adds one reason at a time: the one that unlocks the most scripts
    together with those already chosen; when none unlocks anything alone,
    the one blocking the most still-refused scripts (gain 0 that step)."""
    chosen, climb = set(), []
    unlocked = lambda S: sum(1 for _, ok, c, _ in sets if ok and c and c <= S)  # noqa: E731
    base = sum(1 for _, ok, c, e in sets if not e)
    have = unlocked(chosen)
    for _ in range(limit):
        pending = [c for _, ok, c, _ in sets if ok and c and not c <= chosen]
        candidates = {r for c in pending for r in c} - chosen
        if not candidates:
            break
        scored = sorted(((unlocked(chosen | {r}), sum(1 for c in pending if r in c), r) for r in candidates),
                        key=lambda t: (-t[0], -t[1], t[2]))
        n, _, r = scored[0]
        chosen.add(r)
        climb.append(([r], n - have, base + n))
        have = n
    return climb


def analyse(rows, lists):
    sets = script_sets(rows)
    total = len(sets)
    res = {"scripts": total}
    res["compiled"] = sum(1 for st, ok, c, e in sets if not e and ok and st.get("outcome") != "fallback")
    refused = [(st, ok, c, e) for st, ok, c, e in sets if e]
    res["refused"] = len(refused)
    # Self-check: the inventory's first reason is classify's reason.
    res["first_mismatch"] = [st.get("id") for st in rows if st.get("outcome") == "fallback" and st.get("reasons")
                             and st["reasons"][0]["reason"].split(":")[0] != st["reason"].split(":")[0]]
    res["incomplete"] = collections.Counter(st.get("inventory_stop") for st, ok, c, e in refused if not ok)
    res["first_reason"] = collections.Counter(st.get("reason") for st, ok, c, e in refused)
    res["blocked_by"] = collections.Counter(r for _, _, c, _ in refused for r in c)
    res["blocked_by_any"] = collections.Counter(r for _, _, _, e in refused for r in e)
    res["sole"] = collections.Counter(next(iter(c)) for _, ok, c, _ in refused if ok and len(c) == 1)
    res["distinct"] = collections.Counter(min(len(c), 4) for _, ok, c, _ in refused)
    res["with_consequential"] = sum(1 for _, _, c, e in refused if e - c)
    res["climb"] = greedy(sets)
    cps = collections.defaultdict(set)
    for i, (_, _, c, e) in enumerate(refused):
        for r in e:
            cps[checkpoint(r, lists, e)].add(i)
    res["by_checkpoint"] = {f"{k[0]} | {k[1]}": len(v) for k, v in sorted(cps.items(), key=lambda kv: -len(kv[1]))}
    # Scripts whose every certain reason maps to one checkpoint group.
    only = collections.Counter()
    for _, ok, c, _ in refused:
        groups = {checkpoint(r, lists, c)[0] for r in c}
        if ok and len(groups) == 1:
            only[next(iter(groups))] += 1
    res["only_checkpoint"] = only
    # The same at feature level: each certain reason becomes its feature
    # (checkpoint | scope); sole blockers and the greedy climb over features.
    fsets = []
    for st, ok, c, e in sets:
        fc = frozenset(" | ".join(checkpoint(r, lists, c)) for r in c)
        fe = frozenset(" | ".join(checkpoint(r, lists, e)) for r in e)
        fsets.append((st, ok, fc, fe))
    res["feature_blocked"] = collections.Counter(f for _, _, c, e in fsets if e for f in c)
    res["feature_sole"] = collections.Counter(next(iter(c)) for _, ok, c, e in fsets if e and ok and len(c) == 1)
    res["feature_climb"] = greedy(fsets, limit=25)
    # Whole checkpoints, cumulatively in roadmap order.
    order = ["CP12b", "CP13", "CP14", "CP15", "CP16", "CP16/CP24", "CP22", "CP22/CP24", "CP24", "CP19",
             "language-cleanup", "VM subset", "VM subset (D-018/kinds)", "unassigned", "limits", "none"]
    climb, have = [], set()
    for cp in order:
        have.add(cp)
        n = sum(1 for _, ok, c, e in sets if e and ok and {checkpoint(r, lists, c)[0] for r in c} <= have)
        climb.append((cp, n))
    res["checkpoint_climb"] = climb
    return res


def show(name, res, lists):
    p = lambda n: f"{n} ({100.0 * n / max(1, res['scripts']):.1f} %)"  # noqa: E731
    print(f"== {name}: {res['scripts']} scripts; compile on the VM {p(res['compiled'])}; refused {res['refused']}; "
          f"incomplete inventories {dict(res['incomplete'])}; with consequential reasons {res['with_consequential']}; "
          f"first-reason mismatches {len(res['first_mismatch'])}")
    print("  distinct certain reasons per refused script (4 = 4+):", dict(sorted(res["distinct"].items())))
    print("  reasons by scripts blocked (certain / any; sole blocker; checkpoint):")
    for r, n in res["blocked_by_any"].most_common(40):
        print(f"    {res['blocked_by'].get(r, 0):4d} / {n:4d}  sole {res['sole'].get(r, 0):4d}  {r:40s} "
              f"{' | '.join(checkpoint(r, lists))}")
    print("  greedy climb (features added, scripts unlocked, cumulative on the VM):")
    for feats, n, cum in res["climb"]:
        print(f"    +{n:4d} -> {p(cum)}  {', '.join(feats)}")
    print("  scripts blocked per checkpoint group (any reason):")
    for k, n in res["by_checkpoint"].items():
        print(f"    {n:4d}  {k}")
    print("  refused scripts whose certain reasons all map to one group:", dict(res["only_checkpoint"].most_common()))
    print("  features (checkpoint | scope): scripts blocked (certain), sole blocker:")
    for f, n in res["feature_blocked"].most_common(30):
        print(f"    {n:4d}  sole {res['feature_sole'].get(f, 0):4d}  {f}")
    print("  greedy climb over features:")
    for feats, n, cum in res["feature_climb"]:
        print(f"    +{n:4d} -> {p(cum)}  {', '.join(feats)}")
    print("  whole checkpoints, cumulative (refused scripts unlocked; on the VM):")
    for cp, n in res["checkpoint_climb"]:
        print(f"    {cp:28s} +{n:4d} -> {p(res['compiled'] + n)}")


def jsonable(x):
    if isinstance(x, collections.Counter):
        return dict(x.most_common())
    if isinstance(x, dict):
        return {str(k): jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [jsonable(v) for v in x]
    return x


OWNER_ORDER = ["CP12b", "CP12C", "CP13", "CP14", "CP14B", "CP15", "CP15B", "CP16", "CP16/CP20", "CP16/CP24", "CP18",
               "CP19", "CP22", "CP22/CP23", "CP22/CP24", "CP24", "language-cleanup", "unattributed"]


def owners_table(rows, lists):
    """Markdown rows: owner | scope | reasons with the number of scripts
    carrying each; `unattributed` last (never guessed)."""
    groups = collections.defaultdict(collections.Counter)
    for r in rows:
        items = r.get("reasons", [])
        ctx = frozenset(i["reason"] for i in items)
        seen = set()
        for i in items:
            key = (owner(i["reason"], lists, ctx, i), i["reason"])
            if key not in seen:
                seen.add(key)
                groups[key[0]][i["reason"]] += 1
    lines = ["| Owner | Scope | Reasons (scripts carrying the reason) |", "| --- | --- | --- |"]
    rank = {o: k for k, o in enumerate(OWNER_ORDER)}
    for (own, scope), cnt in sorted(groups.items(), key=lambda kv: (rank.get(kv[0][0], len(rank) - 1), kv[0])):
        lines.append(f"| {own} | {scope} | " + ", ".join(f"`{x}` {n}" for x, n in cnt.most_common()) + " |")
    if not any(own == "unattributed" for own, _ in groups):
        lines.append("| unattributed | — | none |")
    return lines


def selftest():
    """Receiver-aware attribution of `method:<name>` (fixed lists, no corpus)."""
    lists = {"CP14": {"join", "stringify", "to_string", "contains"}, "CP15": {"abs"}}
    cases = [
        # A thread handle's method: the refused thread() constructor owns it.
        (("method:join", {"callable:thread", "method:join"}, {"consequential": True}), "CP22/CP23"),
        # A struct instance's method: structs own it.
        (("method:stringify", {"statement:StructDecl", "method:stringify"}, None), "CP12b"),
        # No refused receiver in the script: receiver-kind inference.
        (("method:to_string", {"method:to_string"}, {"consequential": False}), "CP12C"),
        # Receivers of two owners: unattributed, not guessed.
        (("method:contains", {"callable:thread", "statement:StructDecl", "method:contains"}, None), "unattributed"),
        # A masked receiver nothing identifies: unattributed.
        (("method:join", {"method:join", "free-name:undeclared"}, {"consequential": True}), "unattributed"),
        # Unchanged: a method without a native keeps its by-name owner.
        (("method:map", {"method:map"}, None), "CP14B"),
    ]
    failures = 0
    for (reason, ctx, item), want in cases:
        got = owner(reason, lists, frozenset(ctx), item)
        if got[0] != want:
            failures += 1
            print(f"FAIL owner({reason}, {sorted(ctx)}, {item}) = {got}, expected {want}")
    print(f"{'PASS' if not failures else 'FAIL'} cp12b_inventory selftest: {len(cases)} cases, {failures} failure(s)")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("--nift", required=True)
    c.add_argument("--out", required=True)
    c.add_argument("--projunits")
    c.add_argument("--gen", type=int, default=300)
    c.add_argument("--seed", type=int, default=1)
    c.add_argument("--only", choices=["shared", "projects", "generator"])
    a = sub.add_parser("analyse")
    a.add_argument("dir")
    a.add_argument("--json")
    o = sub.add_parser("owners")
    o.add_argument("dir")
    sub.add_parser("selftest")
    args = ap.parse_args()
    if args.cmd == "selftest":
        return selftest()
    if args.cmd == "owners":
        rows = []
        for corpus in ("shared", "projects", "generator"):
            path = os.path.join(args.dir, corpus + ".jsonl")
            if os.path.exists(path):
                rows += [json.loads(line) for line in open(path)]
        print("\n".join(owners_table(rows, load_lists())))
        return 0
    if args.cmd == "collect":
        nift = os.path.abspath(args.nift)
        os.makedirs(args.out, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="cp12b-inv-") as work:
            if args.only in (None, "generator"):
                print("generator:", collect_generator(nift, args.out, args.gen, args.seed, work))
            if args.projunits and args.only in (None, "projects"):
                print("projects:", collect_projects(nift, args.out, args.projunits, work))
            if args.only in (None, "shared"):
                print("shared:", collect_shared(nift, args.out, args.seed, work))
        return 0
    lists = load_lists()
    results = {}
    every = []
    for corpus in ("shared", "projects", "generator"):
        path = os.path.join(args.dir, corpus + ".jsonl")
        if not os.path.exists(path):
            continue
        rows = [json.loads(line) for line in open(path)]
        every += rows if corpus != "generator" else []
        results[corpus] = analyse(rows, lists)
        show(corpus, results[corpus], lists)
        if corpus == "shared":
            # The shared set includes rt-diff's own generated programs
            # (`gen/...`, rt-diff --gen 200): reported apart as well.
            for part, keep in (("shared-curated", lambda r: not r["id"].startswith("gen/")),
                               ("shared-rtdiff-gen", lambda r: r["id"].startswith("gen/"))):
                results[part] = analyse([r for r in rows if keep(r)], lists)
                show(part, results[part], lists)
    if every:
        results["shared+projects"] = analyse(every, lists)
        show("shared+projects", results["shared+projects"], lists)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(jsonable(results), f, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
