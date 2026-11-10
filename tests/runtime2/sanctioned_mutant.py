#!/usr/bin/env python3
"""Build a mutant nift in a sanctioned non-production directory (B15, combined-b3-b5.md section 75).

  sanctioned_mutant.py --out DIR [--config default|pic|san] [--define=-DNAME[=VALUE] ...] [--patch FILE ...]
                       [--source SRC ...] [--objects-env VAR] [--unit TEST.cpp]
  sanctioned_mutant.build(...)  (the same, from Python; also exact text edits)

The production build guard (scripts/build/compile-object.sh --production, tooling.md 3) refuses a
test-only switch (NIFT_*MUTANT*, ...) on a production object, so a mutant is never built by
`make nift` with mutant CXXFLAGS, in this tree or in a copy of it. Instead:

- Only the translation units the mutant changes are compiled, with nift's own compile line
  ($(CXX) $(PRODUCTION_GUARD) $(CPPFLAGS) $(CXXFLAGS), from tests/runtime2/build_toolchain.py) plus
  the mutant's defines, into DIR, which is not a production object path. Every other object is
  nift's own, so the mutant has production-source identity except for what the mutant changes.
- With --define and no --source, the units are every source of nift that names the macro, and every
  source whose compiler-recorded dependencies (its production .d file) include a header naming it.
- --patch applies a unified diff (-p1) to copies of the sources it names, under DIR; a patch that
  no longer applies is a harness failure (a stale mutant), never a detection.
- Each mutant object is compared with nift's (the C75 check): one identical to production is not
  a mutant object and is not linked; if none differs, the mutant is dead and the build fails.
- The link is nift's: $(CXX) $(CXXFLAGS) $(LDFLAGS) <objects> $(LDLIBS). DIR/nift is the binary and
  DIR/provenance.json records the sources, patches, objects and hashes.
Run through make (HARNESS_TOOLCHAIN_ENV) after `make nift`, so nift's objects are current. The
caller deletes DIR afterwards.

--unit TEST.cpp links a unit test instead of nift: $(CXX) $(CPPFLAGS) -Itests/rt $(CXXFLAGS) TEST.cpp
<objects> -ldl $(LIBFFI_A), with the object set named by --objects-env (for example NIFT_TC_RT_UNIT_OBJECTS,
$(RT_UNIT_OBJECTS)); the binary is DIR/unit.

--config pic uses the -fPIC object set of libnift_c ($(C_ABI_PIC), NIFT_TC_PIC_OBJECTS) and adds -fPIC.
--config san builds the mutant of the ASan/UBSan nift ($(SAN_TARGET)) the same way: the sanitizer
compile line ($(CXX) $(PRODUCTION_GUARD) $(SAN_CPPFLAGS) ... $(SANITIZER_FLAGS)), the $(SAN_OBJECTS)
set and its link line (HARNESS_SAN_TOOLCHAIN_ENV); it needs `make .build/nift-sanitize` first.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_toolchain as toolchain  # noqa: E402


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_of(obj):
    """The source of a production object; objects of another configuration (.build/san/, .build/pic/)
    name their source below that directory."""
    m = re.match(r"^(?:\.build|\$\(TEST_DIR\))/(?:san|pic)/(.*)$", obj)
    rel = m.group(1) if m else obj
    for suffix in (".cpp", ".c"):
        if (ROOT / (rel[:-2] + suffix)).exists():
            return rel[:-2] + suffix
    return None  # a test object of the set (a stub): linked as it is, never mutated


def macro_names(defines):
    names = []
    for d in defines:
        m = re.match(r"-D([A-Za-z_][A-Za-z0-9_]*)", d)
        if not m:
            raise SystemExit(f"sanctioned_mutant: not a -D define: {d}")
        names.append(m.group(1))
    return names


def patched_files(patch):
    files = []
    for line in Path(patch).read_text().splitlines():
        if line.startswith("+++ "):
            path = line[4:].split("\t")[0].strip()
            files.append(path[2:] if path.startswith("b/") else path)
    return files


CONFIGS = {
    # name: (objects variable, extra compile flags)
    "default": (None, ()),
    "pic": ("NIFT_TC_PIC_OBJECTS", ("-fPIC",)),
    "san": ("NIFT_TC_SAN_OBJECTS", ()),
}


def build(out, defines=(), patches=(), sources_given=(), edits=(), config="default", objects_env=None, unit=None,
          quiet=False):
    """Build the mutant into `out` (a fresh non-production directory) and return the binary's path.
    `edits`: (source, old, new) exact text changes, each matching exactly once. Raises SystemExit on
    a harness failure (a stale or dead mutant, a failed compile or link)."""
    os.chdir(ROOT)
    out = Path(out).resolve()
    if out == ROOT or ROOT / "src" in out.parents or out.parent == ROOT:
        raise SystemExit(f"sanctioned_mutant: {out} is not a non-production directory")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    san = config == "san"
    objects = toolchain.words(objects_env or CONFIGS[config][0] or "NIFT_TC_CLI_OBJECTS")
    sources = {source_of(o): o for o in objects if source_of(o)}
    names = macro_names(defines)

    units = list(sources_given) + [e[0] for e in edits]
    for patch in patches:
        units += patched_files(patch)
    if not units:
        if not names:
            raise SystemExit("sanctioned_mutant: nothing to mutate (no define, patch, edit or source)")
        headers = {str(h) for h in [*Path("src").rglob("*.h"), *Path("src").rglob("*.inc"), *Path("include").rglob("*.h")]
                   if any(n in h.read_text(errors="replace") for n in names)}
        def depends(source):
            dep = Path(sources[source][:-2] + ".d")
            if not dep.exists():
                raise SystemExit(f"sanctioned_mutant: {dep} is missing; build the production objects first")
            text = dep.read_text().replace("\\\n", " ")
            return any(os.path.normpath(word.rstrip(":")) in headers for word in text.split())
        units = [s for s in sources if any(n in Path(s).read_text(errors="replace") for n in names)
                 or (headers and depends(s))]
        if headers and not quiet:
            print(f"headers naming {' '.join(names)}: {' '.join(sorted(headers))}")
        if not units:
            raise SystemExit(f"sanctioned_mutant: no source of the object set names {names}")
    units = sorted(set(units))
    for unit_source in units:
        if unit_source not in sources:
            raise SystemExit(f"sanctioned_mutant: {unit_source} is not a source of the object set")
        if not Path(sources[unit_source]).exists():
            raise SystemExit(f"sanctioned_mutant: production object {sources[unit_source]} is missing; build "
                             + (".build/nift-sanitize" if san else "the production objects") + " first")

    copies = {}
    if patches or edits:
        tree = out / "tree"
        for unit_source in units:
            (tree / unit_source).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(unit_source, tree / unit_source)
        for patch in patches:
            proc = subprocess.run(["patch", "-p1", "--no-backup-if-mismatch", "-i", str(Path(patch).resolve())],
                                  cwd=tree, capture_output=True, text=True)
            if proc.returncode:
                sys.stderr.write(proc.stdout + proc.stderr)
                raise SystemExit(f"sanctioned_mutant: patch {patch} does not apply to the current source "
                                 "(a stale mutant: a harness failure, not a detection)")
        for path, old, new in edits:
            text = (tree / path).read_text()
            if text.count(old) != 1:
                raise SystemExit(f"sanctioned_mutant: the mutation site matches {text.count(old)} times in {path} "
                                 "(a stale mutant: a harness failure, not a detection)")
            (tree / path).write_text(text.replace(old, new, 1))
        copies = {u: tree / u for u in units}

    record = dict(config=config, defines=list(defines), patches={p: sha(p) for p in patches},
                  edits=[dict(source=e[0], old=e[1], new=e[2]) for e in edits], units=[], unaffected=[],
                  production_nift=sha("nift") if Path("nift").exists() else None)
    substitute = {}
    for unit_source in units:
        production = sources[unit_source]
        obj = out / "obj" / unit_source.rsplit(".", 1)[0]
        obj = obj.with_name(obj.name + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        src = copies.get(unit_source, unit_source)
        quote = ["-iquote", str(Path(unit_source).parent)] if unit_source in copies else []
        line = toolchain.words("NIFT_TC_SAN_COMPILE") if san else toolchain.production_compile(*CONFIGS[config][1])
        toolchain.run([*line, *quote, *defines, "-c", str(src), "-o", str(obj)],
                      "mutant compile " + unit_source, log=str(obj) + ".log")
        if obj.read_bytes() == Path(production).read_bytes():
            record["unaffected"].append(unit_source)
            if not quiet:
                print(f"mutant object {production}: identical to production (not a mutant object; not linked)")
            continue
        substitute[production] = str(obj)
        record["units"].append(dict(source=unit_source, source_sha256=sha(unit_source), compiled=str(src),
                                    compiled_sha256=sha(src), object=str(obj.relative_to(out)),
                                    object_sha256=sha(obj), production_object_sha256=sha(production)))
    if not substitute:
        raise SystemExit("sanctioned_mutant: every mutant object is identical to production (a dead mutant)")
    linked = [substitute.get(o, o) for o in objects]
    binary = out / ("unit" if unit else "nift")
    if unit:
        toolchain.run(toolchain.test_compile("-pthread", unit, *linked, "-ldl", toolchain.libffi_a(), "-o", str(binary)),
                      "mutant unit link")
    elif san:
        toolchain.run([*toolchain.words("NIFT_TC_SAN_LINK"), *linked, toolchain.words("NIFT_TC_SAN_LIBFFI_A")[0],
                       "-o", str(binary)], "mutant link (san)")
    else:
        toolchain.run(toolchain.link(linked, binary), "mutant link")
    record["binary_sha256"] = sha(binary)
    (out / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    label = " ".join([*defines, *[Path(p).name for p in patches], *[f"edit:{e[0]}" for e in edits]])
    if not quiet:
        print(f"built mutant {label}: {binary} ({len(substitute)} mutant object(s), each differs from production) "
              f"sha256={record['binary_sha256'][:16]}")
    return binary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--patch", action="append", default=[])
    ap.add_argument("--source", action="append", default=[])
    ap.add_argument("--config", choices=sorted(CONFIGS), default="default")
    ap.add_argument("--objects-env")
    ap.add_argument("--unit")
    args = ap.parse_args()
    build(args.out, args.define, args.patch, args.source, (), args.config, args.objects_env, args.unit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
