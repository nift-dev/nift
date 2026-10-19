#!/usr/bin/env python3
"""CP12b module-initialisation data: who imports the same module more than once.

  cp12b_imports.py scan ENTRY [--packages DIR]      one program, JSON on stdout
  cp12b_imports.py projects UNITS_DIR PROJECTS_DIR  every CP13 project unit
  cp12b_imports.py corpus DIR...                    every *.f / *.nft under DIRs

Static, nothing executes. For an entry script, the import graph is expanded
the way legacy executes it (every `import`/`@import` statement runs the module
again, src/ParserScript.cpp execute_import_file): the result counts how many
times each module would initialise per run under legacy (re-run on every
import) and under "once per program". Per module: top-level side effects
(output, files/processes, other host effects) and module-level mutable state
(a top-level binding assigned inside one of its functions, or an exported
variable). Imports with a non-literal argument, package imports that cannot
be resolved, imports inside functions/blocks (run count unknown statically)
and cycles are reported, not guessed.
"""
import argparse
import json
import os
import re
import sys

IMPORT = re.compile(r'''(?<![\w.])(@?)import\s*\(\s*(?:"([^"\\]*)"|'([^'\\]*)'|([^)]*))\)''')
FN_START = re.compile(r'(?:^|\n)\s*(?:@fn|fn)\s*(?:\[async\])?\s*\(')
OUTPUT = re.compile(r'(?<![\w.])(print|console|write_line|warn|info)\s*\(')
HOST = re.compile(r'(?<![\w.])(write|ofstream|file|open|mkdir|make_dir|rm|cp|mv|copy|move|remove|touch|run|cmd|system|'
                  r'setenv|unsetenv|cd|thread|sleep)\s*\(')
EXPORT = re.compile(r'(?<![\w.])export\s*\(\s*([A-Za-z_]\w*)\s*\)')
DECL = re.compile(r'(?:^|\n)\s*(?:const\s+|immut\s+)?([A-Za-z_]\w*)\s*:=')


def strip_comments(text):
    out, i, q = [], 0, None
    while i < len(text):
        c = text[i]
        if q:
            out.append(c)
            if c == "\\" and i + 1 < len(text):
                out.append(text[i + 1]); i += 2; continue
            if c == q:
                q = None
        elif c in "\"'":
            q = c; out.append(c)
        elif text.startswith("//", i):
            j = text.find("\n", i); i = len(text) if j < 0 else j; continue
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2); i = len(text) if j < 0 else j + 2; continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def depth_map(text):
    """Brace depth at each offset (quotes skipped)."""
    depth, d, q = [], 0, None
    for i, c in enumerate(text):
        depth.append(d)
        if q:
            if c == q and text[i - 1] != "\\":
                q = None
            continue
        if c in "\"'":
            q = c
        elif c == "{":
            d += 1
        elif c == "}":
            d = max(0, d - 1)
    return depth


def top_level(text):
    """The text outside any braces (function bodies, struct bodies, blocks)."""
    depth = depth_map(text)
    return "".join(c if depth[i] == 0 and c not in "{}" else " " for i, c in enumerate(text))


def function_bodies(text):
    bodies = []
    for m in FN_START.finditer(text):
        j = text.find("{", m.end())
        if j < 0:
            continue
        d, k = 0, j
        while k < len(text):
            if text[k] == "{":
                d += 1
            elif text[k] == "}":
                d -= 1
                if d == 0:
                    break
            k += 1
        bodies.append(text[j:k + 1])
    return bodies


class Program:
    def __init__(self, packages=None):
        self.packages = packages
        self.cache = {}

    def resolve(self, arg, importer):
        if "/" not in arg and "\\" not in arg and not os.path.splitext(arg)[1]:
            if self.packages:
                root = os.path.join(self.packages, "nift-packages_" + arg)
                try:
                    entry = json.load(open(os.path.join(root, "manifest.json")))["entry"]
                    return os.path.realpath(os.path.join(root, entry)), "package"
                except (OSError, ValueError, KeyError):
                    pass
            return None, "package-unresolved"
        path = os.path.join(os.path.dirname(importer), arg.replace("\\", "/"))
        return os.path.realpath(os.path.normpath(path)), "file"

    def module(self, path):
        if path in self.cache:
            return self.cache[path]
        try:
            text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        except OSError:
            self.cache[path] = None
            return None
        top = top_level(text)
        depth = depth_map(text)
        imports = []
        for m in IMPORT.finditer(text):
            arg = m.group(2) if m.group(2) is not None else m.group(3)
            imports.append({"arg": arg, "dynamic": arg is None, "nested": depth[m.start()] > 0})
        bodies = function_bodies(text)
        top_decls = set(DECL.findall(top))
        exports = EXPORT.findall(top)
        assigned_in_fn = set()
        for b in bodies:
            for name in top_decls:
                if re.search(r'(?<![\w.])' + re.escape(name) + r'\s*(=(?!=)|\+=|-=|\*=|/=|%=|\+\+|--|\.push\()', b):
                    assigned_in_fn.add(name)
        info = {
            "output": bool(OUTPUT.search(top)),
            "host": sorted(set(m.group(1) for m in HOST.finditer(top))),
            "mutable_state": sorted(assigned_in_fn),
            "exported_vars": sorted(n for n in exports if n in top_decls),
            "exports": exports,
            "imports": imports,
        }
        self.cache[path] = info
        return info

    def expand(self, entry):
        """Legacy expansion: count initialisations per module."""
        runs, problems, edges = {}, [], []

        def visit(path, stack):
            info = self.module(path)
            if info is None:
                problems.append(("missing", path)); return
            seen_here = {}
            for imp in info["imports"]:
                if imp["dynamic"]:
                    problems.append(("dynamic", path)); continue
                target, kind = self.resolve(imp["arg"], path)
                if target is None:
                    problems.append((kind, imp["arg"])); continue
                if imp["nested"]:
                    problems.append(("nested", target))
                if target in stack:
                    problems.append(("cycle", target)); continue
                edges.append((path, target))
                seen_here[target] = seen_here.get(target, 0) + 1
                runs[target] = runs.get(target, 0) + 1
                visit(target, stack + [target])
            for target, n in seen_here.items():
                if n > 1:
                    problems.append(("repeated-in-one-importer", target))

        entry = os.path.realpath(entry)
        visit(entry, [entry])
        return runs, problems, edges


def scan(entry, packages=None, base=None):
    prog = Program(packages)
    runs, problems, edges = prog.expand(entry)
    rel = (lambda p: os.path.relpath(p, base)) if base else (lambda p: p)
    multi = {p: n for p, n in runs.items() if n > 1}
    modules = {}
    for p, n in runs.items():
        info = prog.module(p) or {}
        effects = []
        if info.get("output"):
            effects.append("output")
        if info.get("host"):
            effects.append("host:" + ",".join(info["host"]))
        if info.get("mutable_state"):
            effects.append("mutable-state:" + ",".join(info["mutable_state"]))
        if info.get("exported_vars"):
            effects.append("exported-vars:" + ",".join(info["exported_vars"]))
        modules[rel(p)] = {"legacy_runs": n, "effects": effects}
    repeated_import = any(k == "repeated-in-one-importer" for k, _ in problems)
    # Would initialising once per program change observable behaviour?
    change = []
    for p, n in multi.items():
        info = prog.module(p) or {}
        if info.get("output"):
            change.append(f"{rel(p)}: top-level output printed {n} times under legacy, once under A")
        if info.get("host"):
            change.append(f"{rel(p)}: top-level host effects ({','.join(info['host'])}) run {n} times under legacy")
        if info.get("mutable_state") or info.get("exported_vars"):
            change.append(f"{rel(p)}: module state shared across importers under A "
                          f"({','.join(info.get('mutable_state', []) + info.get('exported_vars', []))})")
    if repeated_import:
        change.append("a module imported twice by the same importer: legacy fails with "
                      "`export collides with existing binding` (if it exports anything); A would not")
    return {"entry": rel(os.path.realpath(entry)), "imports": len(edges), "modules": modules,
            "multi_initialised": {rel(p): n for p, n in multi.items()},
            "problems": sorted({f"{k}:{rel(v) if os.path.isabs(str(v)) else v}" for k, v in problems}),
            "behaviour_change_under_once": change}


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan"); s.add_argument("entry"); s.add_argument("--packages")
    p = sub.add_parser("projects"); p.add_argument("units"); p.add_argument("projects")
    c = sub.add_parser("corpus"); c.add_argument("dirs", nargs="+")
    a = ap.parse_args()
    if a.cmd == "scan":
        print(json.dumps(scan(a.entry, a.packages), indent=1))
        return 0
    rows = []
    if a.cmd == "projects":
        for line in open(os.path.join(a.units, "units.tsv")):
            if line.startswith("#") or not line.strip():
                continue
            repo, _url, _commit, origin, fixture, kind, _sha = line.rstrip("\n").split("\t")
            path = os.path.join(a.projects, repo, origin.split(":")[0])
            if kind == "template-script" or not os.path.exists(path):
                # @script bodies: scan the extracted unit, resolving imports against the template's directory
                src = os.path.join(a.units, fixture)
                path = src
            r = scan(path, a.projects, a.projects if path.startswith(a.projects) else a.units)
            r["unit"] = fixture
            rows.append(r)
    else:
        for d in a.dirs:
            for root, _dirs, names in os.walk(d):
                for n in sorted(names):
                    if n.endswith((".f", ".nft")):
                        r = scan(os.path.join(root, n), None, d)
                        if r["imports"] or r["problems"]:
                            rows.append(r)
    for r in rows:
        print(json.dumps(r, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
