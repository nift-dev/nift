#!/usr/bin/env python3
"""save() filesystem safety (D-074, D-091 #5; checkpoints/save-safety.md), in both engines.

  save_safety.py --nift NIFT     every case, VM (NIFT_VM=require) and legacy (NIFT_VM=0)
  save_safety.py --mutants       NIFT_SAVE_MUTANT=1..10 and 12, each built with sanctioned_mutant.py, must be
                                 caught; 11 (correct code with the test-only hooks) is the control and must pass
  save_safety.py --nift NIFT --mutants   both (the test-rt-vm suite, REVIEW-C142 M1)

Every fixture is in a fresh temporary directory: no case writes, or opens for writing, any path outside it, and
the harness never opens /dev/stdout (it checks that it is still a link to /proc/self/fd/1). Special files reach
save() through a file opened on an absent name and a mv() of the fixture onto it, so open() never reads them. The
device fixture is a mknod `c 1 3` inside the directory, only stat()ed, removed in a finally block; where mknod is
not permitted it is reported NOT-RUN, never as a pass; so is the non-root case where the harness cannot run nift as
nobody. A link to /proc/self/fd/1 is only created in the directory and renamed there, never opened. Exit status: 0 = every case passes (or every mutant is
caught); 1 = a failure.
"""
import argparse
import os
import resource
import shutil
import socket
import stat
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
NEW = "NEW-CONTENT"
NOBODY = 65534


def program(name, swap=None, write=True, before=""):
    s = before + 'f := file("%s")\nf.open("w")\n' % name
    if write:
        s += 'f.write("%s")\n' % NEW
    if swap:
        s += 'native::mv("%s", "%s")\n' % (swap, name)
    return s + 'f.save()\nf.close()\nprint("saved")\n'


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 << 20, 64 << 20))


def as_user(uid):
    def start():
        limits()
        if uid is not None:
            os.setgid(uid)
            os.setgroups([])
            os.setuid(uid)
    return start


def run(nift, d, src, vm, stdout_name=None, stderr_name=None, uid=None, extra=None):
    with open(os.path.join(d, "main.f"), "w") as f:
        f.write(src)
    env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", "NIFT_VM": "require" if vm else "0", **(extra or {})}
    out = open(os.path.join(d, stdout_name), "a") if stdout_name else subprocess.PIPE
    err = open(os.path.join(d, stderr_name), "a") if stderr_name else subprocess.PIPE
    try:
        p = subprocess.run([nift, "main.f"], cwd=d, env=env, stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                           text=True, timeout=20, preexec_fn=as_user(uid))
    except subprocess.TimeoutExpired:
        return None, "", "timeout (a special file was opened?)"
    finally:
        for h in (out, err):
            if h is not subprocess.PIPE:
                h.close()
    read = lambda n, s: open(os.path.join(d, n)).read() if n else (s or "")
    return p.returncode, read(stdout_name, p.stdout), read(stderr_name, p.stderr)


def text(d, n):
    with open(os.path.join(d, n)) as f:
        return f.read()


def temps(d):
    return [n for n in os.listdir(d) if ".nift-tmp-" in n]


def refused(message, kept):
    """A refusal: exit 1 with `message`, no saved line, no temporary file, then kept(d) for the target."""
    def check(d, rc, out, err, ino):
        p = [] if rc == 1 and message in err and "saved" not in out else ["rc %s, stderr %r" % (rc, err[-300:])]
        return p + (["temporary file left: %s" % temps(d)] if temps(d) else []) + kept(d, ino)
    return check


def fixture(kind, path):
    if kind == "fifo":
        os.mkfifo(path)
    elif kind == "socket":
        s = socket.socket(socket.AF_UNIX)
        s.bind(path)
        s.close()
    elif kind == "device":
        os.mknod(path, stat.S_IFCHR | 0o600, os.makedev(1, 3))  # c 1 3, in the scratch directory only


def is_type(kind, path):
    test = {"fifo": stat.S_ISFIFO, "socket": stat.S_ISSOCK, "device": stat.S_ISCHR}[kind]
    return test(os.lstat(path).st_mode)


def special(kind, via_link):
    def setup(d):
        fixture(kind, os.path.join(d, "node"))
        os.symlink("node", os.path.join(d, "swap")) if via_link else os.rename(os.path.join(d, "node"), os.path.join(d, "swap"))
    def kept(d, ino):
        node = "node" if via_link else "p"
        bad = [] if os.path.lexists(os.path.join(d, node)) and is_type(kind, os.path.join(d, node)) else ["%s replaced" % kind]
        return bad + ([] if not via_link or os.path.islink(os.path.join(d, "p")) else ["the link was replaced"])
    return setup, program("p", swap="swap"), refused("save: target is not a regular file", kept), kind


def same(n, content, mode=None):
    def kept(d, ino):
        st = os.lstat(os.path.join(d, n))
        return ([] if text(d, n) == content else ["%s changed: %r" % (n, text(d, n))]) + \
               ([] if ino.get(n) == st.st_ino else ["%s replaced (inode)" % n]) + \
               ([] if mode is None or stat.S_IMODE(st.st_mode) == mode else ["%s mode %o" % (n, st.st_mode)])
    return kept


def saved(n, others=(), link=None, mode=0o640, inode="new", links=()):
    def check(d, rc, out, err, ino):
        p = [] if rc == 0 and "saved" in out else ["rc %s, stderr %r" % (rc, err[-300:])]
        p += [] if text(d, n) == NEW else ["%s not saved: %r" % (n, text(d, n))]
        st = os.stat(os.path.join(d, n))
        p += [] if mode is None or stat.S_IMODE(st.st_mode) == mode else ["mode %o, expected %o" % (stat.S_IMODE(st.st_mode), mode)]
        if inode == "new" and ino.get(n) == st.st_ino:
            p.append("%s written in place (same inode), not atomically replaced" % n)
        for o in others:
            p += [] if text(d, o) == "old" else ["%s (hard link) changed to %r" % (o, text(d, o))]
        for name, to in ([(link, n)] if link else []) + list(links):
            p += [] if os.path.islink(os.path.join(d, name)) and os.readlink(os.path.join(d, name)) == to else \
                ["%s is no longer a link to %s" % (name, to)]
        return p + (["temporary file left: %s" % temps(d)] if temps(d) else [])
    return check


def write(d, n, content="old", mode=0o640):
    with open(os.path.join(d, n), "w") as f:
        f.write(content)
    os.chmod(os.path.join(d, n), mode)


def std(name, target):
    """stdout or stderr redirected to the scratch file std.txt; the program saves to `target` (it or a link)."""
    def setup(d):
        write(d, "std.txt", "")
        if target != "std.txt":
            os.symlink("std.txt", os.path.join(d, target))
    return setup, program(target, before='print("before")\n'), None, name  # its check is made in case()


CASES = {
    "regular": (lambda d: write(d, "t.txt"), program("t.txt"), saved("t.txt")),
    "new-file": (lambda d: None, program("t.txt"), saved("t.txt", mode=None, inode="any")),
    "hard-link": (lambda d: (write(d, "t.txt"), os.link(os.path.join(d, "t.txt"), os.path.join(d, "h.txt"))),
                  program("t.txt"), saved("t.txt", others=["h.txt"])),
    "symlink": (lambda d: (write(d, "t.txt"), os.symlink("t.txt", os.path.join(d, "l.txt"))), program("l.txt"),
                saved("t.txt", link="l.txt")),
    "dangling": (lambda d: os.symlink("missing.txt", os.path.join(d, "l.txt")), program("l.txt"),
                 refused("save: dangling symlink", lambda d, ino: [] if os.path.islink(os.path.join(d, "l.txt"))
                         and not os.path.lexists(os.path.join(d, "missing.txt")) else ["the link or its target changed"])),
    "read-only": (lambda d: write(d, "r.txt", mode=0o444), program("r.txt"),
                  refused("save: target is read-only", same("r.txt", "old", 0o444))),
    "read-only-unchanged": (lambda d: write(d, "r.txt", mode=0o444), program("r.txt", write=False),
                            refused("save: target is read-only", same("r.txt", "old", 0o444))),
    # REVIEW-C142 L3: a chain, a loop, a link to a read-only file, the non-root access(W_OK) rule
    "symlink-chain": (lambda d: (write(d, "t.txt"), os.symlink("t.txt", os.path.join(d, "l2.txt")),
                                 os.symlink("l2.txt", os.path.join(d, "l1.txt"))), program("l1.txt"),
                      saved("t.txt", links=[("l1.txt", "l2.txt"), ("l2.txt", "t.txt")])),
    "symlink-loop": (lambda d: (os.symlink("b.txt", os.path.join(d, "a.txt")), os.symlink("a.txt", os.path.join(d, "b.txt"))),
                     program("a.txt"), refused("save: dangling symlink", lambda d, ino: [] if all(
                         os.path.islink(os.path.join(d, n)) for n in ("a.txt", "b.txt")) else ["a link was replaced"])),
    "symlink-read-only": (lambda d: (write(d, "r.txt", mode=0o444), os.symlink("r.txt", os.path.join(d, "l.txt"))),
                          program("l.txt"), refused("save: target is read-only", lambda d, ino: same("r.txt", "old", 0o444)(
                              d, ino) + ([] if os.path.islink(os.path.join(d, "l.txt")) else ["the link was replaced"]))),
    "non-root-access": (lambda d: nobody(d), program("r.txt"),
                        refused("save: target is read-only", same("r.txt", "old", 0o644)), NOBODY),
    # REVIEW-C142 L1: an lstat error other than ENOENT (a file used as a directory) is refused, not a new file
    "not-a-directory": (lambda d: write(d, "f.txt"), program("f.txt/x"),
                        refused("save: cannot check the target: Not a directory", same("f.txt", "old", 0o640))),
    # REVIEW-C142 L4: stdout a pipe, saved through a link to /proc/self/fd/1 (renamed in, never opened)
    "stdout-pipe-link": (lambda d: os.symlink("/proc/self/fd/1", os.path.join(d, "swap")), program("p", swap="swap"),
                         refused("save: target is the process's standard output or error", lambda d, ino: [] if
                                 os.path.islink(os.path.join(d, "p")) and os.readlink(os.path.join(d, "p")) ==
                                 "/proc/self/fd/1" else ["the link was replaced"])),
}


def nobody(d):
    """r.txt owned by root, 0644, in a directory anyone may write (not sticky): only access(W_OK) refuses it."""
    if os.geteuid() != 0:
        raise PermissionError("the harness is not root, so it cannot run nift as nobody")
    os.chmod(d, 0o777)
    write(d, "r.txt", mode=0o644)
    probe = subprocess.run([NIFT[0], "--version"], cwd=d, preexec_fn=as_user(NOBODY), capture_output=True)
    if probe.returncode != 0:
        raise PermissionError("nobody cannot run %s" % NIFT[0])


NIFT = [None]
CHANGED = "save: target changed during save"


def is_link(d, n, to):
    return os.path.islink(os.path.join(d, n)) and os.readlink(os.path.join(d, n)) == to


def expect(cond, problem):
    return [] if cond else [problem]


def original(d, ino, n, was):
    """`n` is the object that was `was` before the run, unchanged: the same inode and the old content."""
    return expect(os.lstat(os.path.join(d, n)).st_ino == ino[was] and text(d, n) == "old", "%s is not the original" % n)


# D-093 (REVIEW-C142 (d)1), hook builds only (NIFT_SAVE_MUTANT 9-12): NIFT_TEST_SAVE_SWAP runs in the program's
# directory at the pre-rename point. Each swap must be refused, the swapped and the original objects unchanged.
RACE = {
    "race-fifo": (lambda d: write(d, "t.txt"), program("t.txt"), "mv t.txt keep.txt && mkfifo t.txt",
                  lambda d, ino: expect(is_type("fifo", os.path.join(d, "t.txt")), "t.txt is no longer the FIFO") +
                  original(d, ino, "keep.txt", "t.txt")),
    "race-read-only": (lambda d: write(d, "t.txt"), program("t.txt"),
                       "mv t.txt keep.txt && printf other > t.txt && chmod 444 t.txt",
                       lambda d, ino: expect(text(d, "t.txt") == "other" and stat.S_IMODE(os.stat(os.path.join(
                           d, "t.txt")).st_mode) == 0o444, "the read-only file changed") + original(d, ino, "keep.txt", "t.txt")),
    "race-new-inode": (lambda d: write(d, "t.txt"), program("t.txt"), "mv t.txt keep.txt && printf other > t.txt",
                       lambda d, ino: expect(text(d, "t.txt") == "other", "the new file changed") +
                       original(d, ino, "keep.txt", "t.txt")),
    "race-symlink": (lambda d: write(d, "t.txt"), program("t.txt"), "mv t.txt keep.txt && ln -s keep.txt t.txt",
                     lambda d, ino: expect(is_link(d, "t.txt", "keep.txt"), "the link was replaced") +
                     original(d, ino, "keep.txt", "t.txt")),
    "race-retarget": (lambda d: (write(d, "t.txt"), write(d, "u.txt", "other"), os.symlink("t.txt", os.path.join(d, "l.txt"))),
                      program("l.txt"), "ln -sfn u.txt l.txt",
                      lambda d, ino: expect(is_link(d, "l.txt", "u.txt") and text(d, "u.txt") == "other",
                                            "the retargeted link or its target changed") + original(d, ino, "t.txt", "t.txt")),
    "race-appear": (lambda d: None, program("n.txt"), "printf other > n.txt",
                    lambda d, ino: expect(text(d, "n.txt") == "other", "the file that appeared changed")),
}
# D-093 (REVIEW-C142 (d)3): the hook builds fix the temporary name; an occupied name fails the save, with nothing
# truncated, written through or created.
SQUAT = "t.txt.nift-tmp-fixed"
OCCUPIED = {
    "occupied-regular": (lambda d: write(d, SQUAT, "squat"),
                         lambda d, ino: expect(text(d, SQUAT) == "squat" and os.lstat(os.path.join(d, SQUAT)).st_ino ==
                                               ino[SQUAT], "the squatting file changed")),
    "occupied-link": (lambda d: (write(d, "other.txt", "other"), os.symlink("other.txt", os.path.join(d, SQUAT))),
                      lambda d, ino: expect(is_link(d, SQUAT, "other.txt") and text(d, "other.txt") == "other",
                                            "written through the link at the temporary name")),
    "occupied-dangling": (lambda d: os.symlink("nowhere.txt", os.path.join(d, SQUAT)),
                          lambda d, ino: expect(is_link(d, SQUAT, "nowhere.txt") and not os.path.lexists(
                              os.path.join(d, "nowhere.txt")), "created through the dangling link")),
}


def hook_case(nift, name, vm):
    d = tempfile.mkdtemp(prefix="save-")
    try:
        if name in RACE:
            setup, src, swap, kept = RACE[name]
            message, extra = CHANGED, {"NIFT_TEST_SAVE_SWAP": swap}
        else:
            setup, kept = OCCUPIED[name]
            src, message, extra = program("t.txt"), "save: cannot create temporary file", {}
            write(d, "t.txt")
        setup(d)
        ino = {n: os.lstat(os.path.join(d, n)).st_ino for n in os.listdir(d)}
        rc, out, err = run(nift, d, src, vm, extra=extra)
        p = expect(rc == 1 and message in err and "saved" not in out, "rc %s, stderr %r" % (rc, err[-300:]))
        try:  # a fixture that a broken save removed is a failure, not a harness error
            p += expect(name in RACE or original(d, ino, "t.txt", "t.txt") == [], "t.txt changed")
            p += kept(d, ino)
        except OSError as e:
            p.append("a fixture is gone: %s" % e)
        leftover = [n for n in temps(d) if n != SQUAT]
        return p + expect(not leftover, "temporary file left: %s" % leftover)
    finally:
        shutil.rmtree(d, ignore_errors=True)
SPECIAL = {"fifo": special("fifo", False), "socket": special("socket", False), "device": special("device", False),
           "symlink-fifo": special("fifo", True), "symlink-device": special("device", True)}
STD = {"stdout": std("stdout", "std.txt"), "stderr": std("stderr", "std.txt"), "symlink-stdout": std("stdout", "ls.txt")}


def case(nift, name, vm):
    """Problems (a list), or None when the fixture could not be made (NOT-RUN)."""
    d = tempfile.mkdtemp(prefix="save-")
    try:
        uid = None
        if name in CASES:
            setup, src, check = CASES[name][:3]
            uid = CASES[name][3] if len(CASES[name]) > 3 else None
            stream = None
        elif name in SPECIAL:
            setup, src, check, _ = SPECIAL[name]
            stream = None
        else:
            setup, src, check, stream = STD[name]
        try:
            setup(d)
        except PermissionError as e:
            print("NOT-RUN save-safety %s %s: fixture not permitted here (%s)" % ("vm" if vm else "legacy", name, e))
            return None
        if name in STD:  # same("std.txt", None): the inode and the program output only
            check = refused("save: target is the process's standard output or error", lambda d, ino: (
                [] if os.lstat(os.path.join(d, "std.txt")).st_ino == ino["std.txt"] else ["std.txt replaced"]) +
                ([] if (text(d, "std.txt").startswith("before") if stream == "stdout" else NEW not in text(d, "std.txt"))
                 else ["std.txt content %r" % text(d, "std.txt")]))
        ino = {n: os.lstat(os.path.join(d, n)).st_ino for n in os.listdir(d)}
        rc, out, err = run(nift, d, src, vm, stdout_name="std.txt" if stream == "stdout" else None,
                           stderr_name="std.txt" if stream == "stderr" else None, uid=uid)
        return check(d, rc, out, err, ino)
    finally:
        dev = [os.path.join(d, n) for n in ("node", "p", "swap") if os.path.lexists(os.path.join(d, n))]
        for p in dev:
            if stat.S_ISCHR(os.lstat(p).st_mode):
                os.unlink(p)  # the c 1 3 node: removed, never opened
        shutil.rmtree(d, ignore_errors=True)


def stdout_link():
    return os.path.islink("/dev/stdout") and os.readlink("/dev/stdout") == "/proc/self/fd/1"


def check(nift, hooks=False):
    """The names of the failing cases (engine-qualified), after printing every result; with `hooks` (a hook build),
    the race and occupied-name cases too."""
    if not stdout_link():
        print("FAIL save-safety: /dev/stdout is not the link to /proc/self/fd/1 before the run")
        return ["environment"], []
    NIFT[0] = nift
    failing, total, notrun = [], 0, []
    for name in list(CASES) + list(SPECIAL) + list(STD) + (list(RACE) + list(OCCUPIED) if hooks else []):
        for vm in (True, False):
            label = "%s %s" % ("vm" if vm else "legacy", name)
            problems = hook_case(nift, name, vm) if name in RACE or name in OCCUPIED else case(nift, name, vm)
            if problems is None:
                notrun.append(label)
                continue
            total += 1
            print("%s save-safety %s%s" % ("FAIL" if problems else "PASS", label, (": " + "; ".join(problems)) if problems else ""))
            if problems:
                failing.append(label)
    if not stdout_link():
        print("FAIL save-safety: /dev/stdout changed during the run")
        failing.append("environment")
    print("save-safety: %d/%d pass, %d not run" % (total - len(failing), total, len(notrun)))
    return failing, notrun


# mutant: the engine-qualified cases that must fail (6 and 7 are in the VM's save() only; the rest in the shared helper)
MUTANTS = {
    1: ["vm dangling", "legacy dangling", "vm symlink-loop", "legacy symlink-loop"],
    2: ["vm stdout", "vm stderr", "vm symlink-stdout", "legacy stdout", "legacy stderr", "legacy symlink-stdout",
        "vm stdout-pipe-link", "legacy stdout-pipe-link"],
    3: ["vm fifo", "vm socket", "vm symlink-fifo", "legacy fifo", "legacy socket", "legacy symlink-fifo"],
    4: ["vm read-only", "vm read-only-unchanged", "legacy read-only", "legacy read-only-unchanged",
        "vm symlink-read-only", "legacy symlink-read-only", "vm non-root-access", "legacy non-root-access"],
    5: ["vm symlink", "legacy symlink", "vm symlink-chain", "legacy symlink-chain"],
    6: ["vm dangling", "vm read-only", "vm fifo", "vm stdout"],
    7: ["vm hard-link", "vm regular"],
    8: ["vm not-a-directory", "legacy not-a-directory"],  # REVIEW-C142 L1 reverted: any lstat error a new file
    # 9-12 are hook builds (the swap hook, the fixed temporary name); 11 is the control: correct code, which must pass
    9: ["%s %s" % (e, c) for e in ("vm", "legacy") for c in ("race-fifo", "race-read-only", "race-new-inode",
                                                            "race-symlink", "race-appear")],  # no revalidation
    10: ["vm race-retarget", "legacy race-retarget"],  # no link re-check
    11: [],
    12: ["%s %s" % (e, c) for e in ("vm", "legacy") for c in OCCUPIED],  # a non-exclusive temporary file
}
HOOK_BUILDS = (9, 10, 11, 12)


def mutants():
    missed_mutants, control = 0, "not run"
    for n, expected in sorted(MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "save-safety-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_SAVE_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                print("FAIL save-safety build %d: harness: build failed: %s" % (n, b.stderr[-400:] or b.stdout[-400:]))
                missed_mutants += n != 11
                control = "build failed" if n == 11 else control
                continue
            failing, notrun = check(os.path.join(out, "nift"), hooks=n in HOOK_BUILDS)
            if n == 11:  # the control: correct code with the hooks, every case (the race cases included) passes
                control = "FAIL: " + ", ".join(failing) if failing else "PASS"
                print("%s save-safety hook control (build 11): %s" % ("FAIL" if failing else "PASS",
                                                                      ", ".join(failing) or "every case passes"))
                continue
            missed = [c for c in expected if c not in failing and c not in notrun]  # a NOT-RUN case is listed above
            if missed:
                print("FAIL save-safety mutant %d: not caught by %s" % (n, missed))
                missed_mutants += 1
            else:
                print("PASS save-safety mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    bad = missed_mutants or control != "PASS"
    print("%s save-safety mutants: %d of %d caught; hook control: %s" % (
        "FAIL" if bad else "PASS", len(MUTANTS) - 1 - missed_mutants, len(MUTANTS) - 1, control))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    rc = 1 if args.nift and check(os.path.abspath(args.nift))[0] else 0
    return max(rc, mutants()) if args.mutants else rc


if __name__ == "__main__":
    sys.exit(main())
