#!/usr/bin/env python3
"""Mutant-list lint (review of C67-C71, must-fix 1; combined-b3-b5.md section 61).

A mutant family whose build recompiles an EXPLICIT list of objects with its macro silently
loses every hook in a source that is not on the list: the macro is undefined there and the hook
compiles to nothing (VM mutant 5 lived in vm_report.cpp, split out of vm.cpp in 94873269, and
was dead until this lint). For each family below, every source that mentions the family's
macro or hook, directly or through a header that does, must be on the family's list.

  check-mutant-lists.py [--makefile Makefile] [--root .]
  check-mutant-lists.py --selftest     (negative controls: a hook outside the list is caught)
"""
import argparse
import os
import re
import shutil
import sys
import tempfile

# family -> (pattern of a hook use in a source, how the Makefile lists the mutated sources)
#   ("var", NAME): a make variable listing src/rt/<x>.o or .cpp
#   ("rules", DIRVAR): rules `$(DIRVAR)/<x>.o: src/rt/<x>.cpp`
FAMILIES = {
    # VM_HOOKS: the macros src/rt/vm_mutants.h defines, read from it at run time.
    "NIFT_VM_MUTANTS": (r"\bNIFT_VM_MUTANTS\b|\bvm_mutant\s*\(\s*\)\s*[=!]=|VM_HOOKS",
                        ("var", "RT_VM_MUTATED")),
    "NIFT_CORO_MUTANT": (r"\bNIFT_CORO_MUTANT\b", ("var", "CP22_CORO_SOURCES", "CORO_MODULE_SOURCES")),
    "NIFT_CORO_MODULE_MUTANT": (r"\bNIFT_CORO_MODULE_MUTANT\b", ("var", "CORO_MODULE_SOURCES")),
    "NIFT_CLOSURE_MUTANT": (r"\bNIFT_CLOSURE_MUTANT\b", ("rules", "CLOSURE_MUTANT_DIR")),
    "NIFT_FOR_MUTANT": (r"\bNIFT_FOR_MUTANT\b", ("rules", "FOR_MUTANT_DIR")),
}
# The header that defines a family's hooks is where the macro is tested, not a use.
# vm_mutants.h defines the VM hooks; vm_ops.h only declares the selector vm_mutant() under the guard.
DEFINING_HEADERS = {"src/rt/vm_mutants.h", "src/rt/vm_ops.h"}
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def make_variable(text, name):
    m = re.search(r"^%s\s*[:+]?=\s*((?:.*\\\n)*.*)$" % re.escape(name), text, re.M)
    if not m:
        return None
    return {re.sub(r"\.o$", ".cpp", w) for w in m.group(1).replace("\\\n", " ").split() if w.startswith("src/")}


def rule_sources(text, dirvar):
    return set(re.findall(r"^\$\(%s\)/[\w.-]+\.o:\s*(src/[\w/.-]+\.cpp)" % re.escape(dirvar), text, re.M))


COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
DEFAULTING = re.compile(r"^\s*#\s*(ifndef|define|endif)\b[^\n]*$", re.M)


def code_of(body, header):
    """The text that can use a hook: comments removed; in a header also the `#ifndef X /
    #define X 0` lines that only give the macro a default."""
    body = COMMENT.sub("", body)
    return DEFAULTING.sub("", body) if header else body


def vm_hook_names(root):
    path = os.path.join(root, "src", "rt", "vm_mutants.h")
    if not os.path.exists(path):
        return []
    with open(path) as handle:
        text = handle.read()
    return sorted(set(re.findall(r"^#define\s+([A-Z_]+)\s*\(", text, re.M)))


def sources(root):
    for top in ("src",):
        for d, _, names in os.walk(os.path.join(root, top)):
            for n in names:
                if n.endswith((".cpp", ".h")):
                    path = os.path.join(d, n)
                    yield os.path.relpath(path, root).replace(os.sep, "/"), path


def check(root, makefile):
    with open(makefile) as handle:
        text = handle.read()
    files = {}
    for rel, path in sources(root):
        with open(path, errors="replace") as handle:
            files[rel] = handle.read()
    problems = []
    hooks = vm_hook_names(root)
    for family, (pattern, listing) in FAMILIES.items():
        if "VM_HOOKS" in pattern:
            pattern = pattern.replace("VM_HOOKS", r"\b(?:%s)\s*\(" % "|".join(hooks) if hooks else "(?!)")
        hook = re.compile(pattern)
        listed = set()
        for name in listing[1:]:
            got = make_variable(text, name) if listing[0] == "var" else rule_sources(text, name)
            if not got:
                problems.append("%s: the Makefile has no list %s" % (family, name))
                continue
            listed |= got
        # headers that use the hooks (not the defining header), closed over includes
        users = {rel for rel, body in files.items()
                 if rel.endswith(".h") and rel not in DEFINING_HEADERS and hook.search(code_of(body, True))}
        changed = True
        while changed:
            changed = False
            for rel, body in files.items():
                if rel.endswith(".h") and rel not in users and rel not in DEFINING_HEADERS:
                    if any(("src/" + inc) in users for inc in INCLUDE.findall(body)):
                        users.add(rel)
                        changed = True
        for rel, body in sorted(files.items()):
            if not rel.endswith(".cpp"):
                continue
            uses = hook.search(code_of(body, False)) or any(("src/" + inc) in users for inc in INCLUDE.findall(body))
            if uses and rel not in listed:
                problems.append("%s: %s uses the family's hooks but is not on its mutated list (%s); "
                                "its hooks compile to nothing in the mutant build" % (family, rel, ", ".join(listing[1:])))
    return problems


def selftest(root, makefile):
    failures = 0
    scratch = tempfile.mkdtemp(prefix="mutant-lists-selftest-")
    try:
        fake = os.path.join(scratch, "src", "rt")
        os.makedirs(fake)
        shutil.copy(makefile, os.path.join(scratch, "Makefile"))
        with open(os.path.join(fake, "vm_mutants.h"), "w") as h:
            h.write("#ifdef NIFT_VM_MUTANTS\n#define MUTATE_ERROR_OFFSET(o) ++o;\n#else\n#define MUTATE_ERROR_OFFSET(o)\n#endif\n")
        cases = {
            "hook outside the VM list": ("vm_split.cpp", '#include "rt/vm_mutants.h"\nint f(int o) { MUTATE_ERROR_OFFSET(o) return o; }\n', "NIFT_VM_MUTANTS: src/rt/vm_split.cpp"),
            "direct vm_mutant() call": ("vm_other.cpp", "int vm_mutant(); int g() { return vm_mutant() == 7; }\n", "NIFT_VM_MUTANTS: src/rt/vm_other.cpp"),
            "coro macro outside its lists": ("coro_new.cpp", "#if defined(NIFT_CORO_MUTANT)\n#endif\n", "NIFT_CORO_MUTANT: src/rt/coro_new.cpp"),
            "for macro outside its rules": ("loops_new.cpp", "#if NIFT_FOR_MUTANT == 2\n#endif\n", "NIFT_FOR_MUTANT: src/rt/loops_new.cpp"),
        }
        for name, (fname, body, expect) in cases.items():
            path = os.path.join(fake, fname)
            with open(path, "w") as h:
                h.write(body)
            found = check(scratch, os.path.join(scratch, "Makefile"))
            ok = any(expect in p for p in found)
            print("%s selftest %s: %s" % ("PASS" if ok else "FAIL", name, "rejected" if ok else "NOT caught"))
            failures += 0 if ok else 1
            os.remove(path)
        clean = [p for p in check(scratch, os.path.join(scratch, "Makefile"))]
        print("%s selftest control: %s" % ("PASS" if not clean else "FAIL", "no finding without a stray hook" if not clean else clean))
        failures += 1 if clean else 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--makefile", default="Makefile")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        failures = selftest(a.root, a.makefile)
        print("%s mutant-list lint selftest: %d failure(s)" % ("PASS" if not failures else "FAIL", failures))
        return 1 if failures else 0
    problems = check(a.root, a.makefile)
    for p in problems:
        print("FAIL " + p)
    if problems:
        return 1
    print("PASS mutant-list lint: every hook of %s is in a mutated object" % ", ".join(FAMILIES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
