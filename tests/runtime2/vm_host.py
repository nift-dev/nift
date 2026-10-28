#!/usr/bin/env python3
"""CP16 VM host built-ins: differential tests in five execution contexts.

A supported case runs on legacy and on the VM (NIFT_VM=require) in two copies
of the same sandbox. stdout, stderr, exit status, final file tree and the
Host-op trace must be identical after normalising the sandbox root. Error
cases must execute on the VM and report legacy's error, never fall back.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# (name, setup function relative to root, body)
def setup_symlinks(root):
    (root / "a").mkdir()
    (root / "a" / "x").write_text("x\n")
    (root / "a" / "y").write_text("y\n")
    os.symlink("a", root / "b")
    (root / "c").mkdir()
    os.symlink("../a/x", root / "c" / "lx")
    (root / "nested").mkdir()
    os.symlink("../a", root / "nested" / "linkdir")


def setup_files(root):
    (root / "text.txt").write_text("one\ntwo\n")
    (root / "bin.dat").write_bytes(b"a\x00b\xff")
    (root / "data.json").write_text('{"a": 1, "b": [true, null]}')
    (root / "sub").mkdir()
    (root / "sub" / "inner.txt").write_text("inner\n")
    (root / "sub" / "z.txt").write_text("z\n")
    (root / "sub" / "a.txt").write_text("a\n")


SUPPORTED = [
    ("open", 'print(open("text.txt"))'),
    ("cat", 'cat("text.txt")'),
    ("sha256_file", 'print(sha256_file("text.txt"))'),
    ("exists", 'print(exists("text.txt")); print(exists("missing"))'),
    ("is_file", 'print(is_file("text.txt")); print(is_file("sub"))'),
    ("is_dir", 'print(is_dir("sub")); print(is_dir("text.txt"))'),
    ("stat", 's := stat("text.txt"); print(s.exists); print(s.type); print(s.size)'),
    ("ls_dir", 'l := ls("sub"); print(l.stringify())'),
    ("ls_glob", 'l := ls("sub/*.txt"); print(l.stringify())'),
    ("pwd", 'print(pwd() != "")'),
    ("cd", 'cd("sub"); print(exists("inner.txt")); cd("..")'),
    ("touch", 'touch("new.txt"); print(exists("new.txt"))'),
    ("mkdir", 'mkdir("made"); print(is_dir("made"))'),
    ("make_dir", 'make_dir("made2"); print(is_dir("made2"))'),
    ("rm", 'touch("gone.txt"); rm("gone.txt"); print(exists("gone.txt"))'),
    ("remove", 'touch("gone2.txt"); remove("gone2.txt"); print(exists("gone2.txt"))'),
    ("cp", 'cp("text.txt", "copy.txt"); print(open("copy.txt"))'),
    ("mv", 'mv("text.txt", "moved.txt"); print(open("moved.txt"))'),
    ("move", 'move("text.txt", "moved.txt"); print(open("moved.txt"))'),
    ("which", 'print(which("ls")); print(which("nope-not-a-command"))'),
    ("getenv", 'print(getenv("NIFT_HOST_TEST_UNSET")); setenv("NIFT_HOST_TEST", "v"); '
               'print(getenv("NIFT_HOST_TEST")); unsetenv("NIFT_HOST_TEST"); '
               'print(getenv("NIFT_HOST_TEST"))'),
    ("env", 'e := env(); print(type(e)); print(e.stringify().length() > 0)'),
    ("os_arch", 'print(os()); print(arch())'),
    ("platform_hw", 'print(platform()); print(hardware_concurrency() >= 1)'),
    ("time", 'print(epoch() > 0); sleep(1); print("slept")'),
    ("file_handle", 'f := file("text.txt")'),
]

SYMLINK = [
    ("symlink_star_x", 'l := ls("*/x"); print(l.stringify())'),
    ("symlink_star_star", 'k := ls("*/*"); print(k.stringify())'),
    ("symlink_nested_file", 'n := ls("c/*"); print(n.stringify())'),
    ("symlink_nested_dir", 'd := ls("nested/linkdir/*"); print(d.stringify())'),
    ("symlink_overlap", 'a := ls("a/*"); print(a.stringify()); b := ls("*/y"); print(b.stringify())'),
]

ERRORS = [
    ("open_missing", 'print(open("missing.txt"))'),
    ("open_directory", 'print(open("sub"))'),
    ("sha256_missing", 'print(sha256_file("missing.txt"))'),
    ("sha256_directory", 'print(sha256_file("sub"))'),
    ("rm_directory", 'rm("sub")'),
    ("cp_missing", 'cp("missing.txt", "out.txt")'),
    ("cd_missing", 'cd("missing-dir")'),
    ("sleep_negative", 'sleep(-1)'),
]

UNSUPPORTED = [
    ("cmd_handle", 'c := cmd("echo")', "callable:cmd"),
    ("timer_handle", 't := timer()', "callable:timer"),
    ("run_process", 'run("echo")', "callable:run"),
    ("inject_dynamic", 'print(inject("data.json"))', "callable:inject"),
    ("read_val_dynamic", 'f := file("text.txt"); f.open("r"); print(f.read_val())', "host-handle-flow"),
]


def contexts(body):
    indented = "".join("  " + line + "\n" for line in body.splitlines())
    return {
        "top": body + "\n",
        "loop": "i := 0\nwhile(i < 1) {\n" + indented + "  i += 1\n}\n",
        "fn-top": "fn(case()) {\n" + indented + "}\ncase()\n",
        "fn-loop": "fn(case()) {\n" + indented + "}\ni := 0\nwhile(i < 1) {\n  case()\n  i += 1\n}\n",
        "nested": "fn(inner()) {\n" + indented + "}\nfn(outer()) {\n  inner()\n}\nouter()\n",
    }


def snapshot(root):
    out = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name in ("case.f", "trace.jsonl", "stats.jsonl"):
            continue
        out[str(path.relative_to(root))] = path.read_bytes()
    return out


def normalise(text, root):
    return text.replace(str(root), "<root>")


def run_both(nift, source, oracle_root, vm_root, extra_env=None, oracle_nift=None):
    oracle_bin = oracle_nift or nift
    for root, vm in ((oracle_root, False), (vm_root, True)):
        (root / "case.f").write_text(source)
        env = dict(os.environ)
        env["NIFT_VM"] = "require" if vm else "0"
        env.pop("NIFT_VM_STATS", None)
        stats = root / "stats.jsonl"
        trace = root / "trace.jsonl"
        env["NIFT_TRACE_HOST"] = "3"
        if extra_env:
            env.update(extra_env)
        if vm:
            env["NIFT_VM_STATS"] = str(stats)
        def preexec():
            os.dup2(trace_fd, 3)
        trace_fd = os.open(trace, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            proc = subprocess.run([nift if vm else oracle_bin, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  preexec_fn=preexec, pass_fds=(trace_fd,))
        finally:
            os.close(trace_fd)
        data = {"out": proc.stdout, "err": proc.stderr, "rc": proc.returncode,
                "trace": trace.read_text(errors="replace").replace(str(root), "<root>"),
                "tree": snapshot(root),
                "stats": stats.read_text(errors="replace") if stats.exists() else ""}
        if vm:
            vm_data = data
        else:
            oracle_data = data
    return oracle_data, vm_data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--oracle", help="unmodified legacy oracle (defaults to --nift)")
    parser.add_argument("--only", choices=["supported", "errors", "unsupported"])
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    nift = os.path.abspath(args.nift)
    oracle_nift = os.path.abspath(args.oracle) if args.oracle else nift
    failures = []
    passed = refused = 0

    def check(name, source, oracle, vm, kind):
        nonlocal passed, refused
        if kind == "supported":
            if vm["stats"].strip():
                try:
                    stat = json.loads(vm["stats"].strip().splitlines()[-1])
                except Exception:
                    stat = {}
                if stat.get("outcome") != "vm":
                    failures.append(f"{name}: VM outcome {stat.get('outcome')} ({stat.get('reason')})")
                    return
            if oracle["out"] != vm["out"] or oracle["rc"] != vm["rc"]:
                failures.append(f"{name}: output/exit differs")
                if args.verbose:
                    print(f"  legacy rc={oracle['rc']} out={oracle['out']!r} err={oracle['err']!r}")
                    print(f"  vm     rc={vm['rc']} out={vm['out']!r} err={vm['err']!r}")
                return
            if oracle["err"] != vm["err"]:
                failures.append(f"{name}: stderr differs")
                return
            if oracle["tree"] != vm["tree"]:
                failures.append(f"{name}: file tree differs")
                return
            if oracle["trace"] != vm["trace"]:
                failures.append(f"{name}: Host trace differs")
                return
            if oracle["trace"] != vm["trace"]:
                failures.append(f"{name}: Host trace differs")
                if args.verbose:
                    print("  legacy trace:", oracle["trace"])
                    print("  vm trace    :", vm["trace"])
                return
            passed += 1
        elif kind == "error":
            if not vm["stats"].strip():
                failures.append(f"{name}: VM has no statistics")
                return
            try:
                stat = json.loads(vm["stats"].strip().splitlines()[-1])
            except Exception:
                failures.append(f"{name}: malformed VM statistics")
                return
            if stat.get("outcome") != "vm-error":
                failures.append(f"{name}: expected vm-error, got {stat.get('outcome')} ({stat.get('reason')})")
                return
            if oracle["out"] != vm["out"] or oracle["rc"] != vm["rc"]:
                failures.append(f"{name}: error output/exit differs")
                return
            # error diagnostics contain absolute sandbox paths; compare final text
            tail_o = oracle["err"].decode(errors="replace").rsplit(": ", 1)[-1].strip()
            tail_v = vm["err"].decode(errors="replace").rsplit(": ", 1)[-1].strip()
            if tail_o != tail_v:
                failures.append(f"{name}: error text differs ({tail_o!r} vs {tail_v!r})")
                return
            if oracle["tree"] != vm["tree"]:
                failures.append(f"{name}: file tree differs")
                return
            if oracle["trace"] != vm["trace"]:
                failures.append(f"{name}: Host trace differs")
                return
            passed += 1
        else:
            if not vm["stats"].strip():
                failures.append(f"{name}: VM has no statistics")
                return
            try:
                stat = json.loads(vm["stats"].strip().splitlines()[-1])
            except Exception:
                failures.append(f"{name}: malformed VM statistics")
                return
            if stat.get("outcome") == "vm" or stat.get("outcome") == "vm-error":
                failures.append(f"{name}: unexpectedly executed on the VM")
                return
            refused += 1

    for name, body in SUPPORTED:
        for context, source in contexts(body).items():
            with tempfile.TemporaryDirectory(prefix="vm-host-") as tmp:
                base = Path(tmp)
                oracle_root = base / "legacy"
                vm_root = base / "vm"
                for root in (oracle_root, vm_root):
                    root.mkdir()
                    setup_files(root)
                oracle, vm = run_both(nift, source, oracle_root, vm_root, oracle_nift=oracle_nift)
                check(f"{name}@{context}", source, oracle, vm, "supported")

    symlink_ok = True
    for name, body in SYMLINK:
        with tempfile.TemporaryDirectory(prefix="vm-host-link-") as tmp:
            base = Path(tmp)
            oracle_root = base / "legacy"
            vm_root = base / "vm"
            try:
                for root in (oracle_root, vm_root):
                    root.mkdir()
                    setup_symlinks(root)
            except (OSError, NotImplementedError):
                symlink_ok = False
                break
            oracle, vm = run_both(nift, body + "\n", oracle_root, vm_root, oracle_nift=oracle_nift)
            check(f"{name}", body, oracle, vm, "supported")
    if not symlink_ok:
        print("SKIP symlink fixtures: symlinks unavailable")

    for name, body in ERRORS:
        with tempfile.TemporaryDirectory(prefix="vm-host-err-") as tmp:
            base = Path(tmp)
            oracle_root = base / "legacy"
            vm_root = base / "vm"
            for root in (oracle_root, vm_root):
                root.mkdir()
                setup_files(root)
            oracle, vm = run_both(nift, body + "\n", oracle_root, vm_root, oracle_nift=oracle_nift)
            check(name, body, oracle, vm, "error")

    root_cases = [
        ("root_exists", 'print(exists("../../victim.txt"))'),
        ("root_is_file", 'print(is_file("../../victim.txt"))'),
        ("root_is_dir", 'print(is_dir("../../victim.txt"))'),
        ("root_stat", 's := stat("../../victim.txt"); print(s.exists)'),
        ("root_open", 'print(open("../../victim.txt"))'),
        ("root_cat", 'cat("../../victim.txt")'),
        ("root_sha", 'print(sha256_file("../../victim.txt"))'),
        ("root_ls_glob", 'l := ls("../../*"); print(l.stringify())'),
        ("root_cd", 'cd("../../")'),
        ("root_touch", 'touch("../../escaped.txt")'),
        ("root_mkdir", 'mkdir("../../escaped-dir")'),
        ("root_rm", 'rm("../../victim.txt")'),
        ("root_rm_glob", 'rm("../../*.txt")'),
        ("root_cp_source", 'cp("../../victim.txt", "copy.txt")'),
        ("root_cp_dest", 'cp("data.txt", "../../copy.txt")'),
        ("root_mv_source", 'mv("../../victim.txt", "moved.txt")'),
        ("root_mv_dest", 'mv("data.txt", "../../moved.txt")'),
    ]
    for name, body in root_cases:
        with tempfile.TemporaryDirectory(prefix="vm-host-root-") as tmp:
            base = Path(tmp)
            oracle_base = base / "legacy-base"
            vm_base = base / "vm-base"
            oracle_root = oracle_base / "root"
            vm_root = vm_base / "root"
            oracle_inside = oracle_root / "inside"
            vm_inside = vm_root / "inside"
            for root, inside in ((oracle_root, oracle_inside), (vm_root, vm_inside)):
                root.mkdir(parents=True)
                inside.mkdir()
                (inside / "data.txt").write_text("data\n")
                (root.parent / "victim.txt").write_text("victim\n")

            def run_engine(root, inside, vm_mode):
                text = body + "\n"
                (inside / "case.f").write_text(text)
                env = dict(os.environ)
                env["NIFT_VM"] = "require" if vm_mode else "0"
                env["NIFT_TRACE_HOST"] = "3"
                env["NIFT_FS_ROOT"] = str(root)
                trace = inside / "trace.jsonl"
                stats = inside / "stats.jsonl"
                if vm_mode:
                    env["NIFT_VM_STATS"] = str(stats)
                fd = os.open(trace, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                try:
                    proc = subprocess.run([nift if vm_mode else oracle_nift, "case.f"], cwd=inside, env=env, stdin=subprocess.DEVNULL,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                          preexec_fn=lambda: os.dup2(fd, 3), pass_fds=(fd,))
                finally:
                    os.close(fd)
                return {"out": proc.stdout,
                        "err": proc.stderr.replace(str(root.parent).encode(), b"<base>").replace(str(root).encode(), b"<root>"),
                        "rc": proc.returncode,
                        "trace": trace.read_text(errors="replace").replace(str(root), "<root>"),
                        "tree": snapshot(root.parent), "stats": stats.read_text() if stats.exists() else ""}

            oracle = run_engine(oracle_root, oracle_inside, False)
            vm = run_engine(vm_root, vm_inside, True)
            check(name, body, oracle, vm, "error")

    for name, body, reason in UNSUPPORTED:
        with tempfile.TemporaryDirectory(prefix="vm-host-unsup-") as tmp:
            base = Path(tmp)
            vm_root = base / "vm"
            vm_root.mkdir()
            setup_files(vm_root)
            (vm_root / "case.f").write_text(body + "\n")
            env = dict(os.environ)
            env["NIFT_VM"] = "require"
            env["NIFT_VM_STATS"] = str(vm_root / "stats.jsonl")
            proc = subprocess.run([nift, "case.f"], cwd=vm_root, env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stats = (vm_root / "stats.jsonl").read_text() if (vm_root / "stats.jsonl").exists() else ""
            if proc.returncode == 0 or reason not in proc.stderr.decode(errors="replace") or not stats:
                failures.append(f"{name}: expected refusal {reason}")
            else:
                refused += 1

    for failure in failures:
        print("FAIL", failure)
    print(f"PASS {passed}, FAIL {len(failures)}, REFUSED {refused}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
