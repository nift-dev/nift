#!/usr/bin/env python3
"""CP20 R2 review: scripted (Host) path fd-leak and child/zombie scan (baseline after one warm-up round).
usage: host_fdleak.py NIFT [ROUNDS]
Runs nift non-interactively (stdin is a pipe), syncs on echo markers, and
counts /proc/<nift>/fd plus nift's children (live/zombie) before and after
ROUNDS of pipelines, exec failures, redirections and background jobs."""
import os, select, subprocess, sys, tempfile, time
NIFT = os.path.abspath(sys.argv[1])
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 40
tmp = tempfile.mkdtemp(prefix="fdleak-")
p = subprocess.Popen([NIFT], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                     cwd=tmp, env=dict(os.environ, HOME=tmp))
buf = b""
n = 0


def sync():
    global buf, n
    n += 1
    mark = f"MARK-{n}".encode()
    p.stdin.write(b"/bin/echo " + mark + b"\n")
    p.stdin.flush()
    end = time.time() + 10
    while mark + b"\n" not in buf and time.time() < end:
        r, _, _ = select.select([p.stdout], [], [], 0.1)
        if r:
            d = os.read(p.stdout.fileno(), 65536)
            if not d:
                break
            buf += d
    assert mark + b"\n" in buf, f"lost sync at {mark!r}: {buf[-200:]!r}"


def fds():
    return sorted(os.listdir(f"/proc/{p.pid}/fd"), key=int)


def kids():
    out = []
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                raw = open(f"/proc/{d}/stat").read()
                f = raw.rsplit(")", 1)[1].split()
                comm = raw[raw.find("(") + 1:raw.rfind(")")]
            except OSError:
                continue
            if int(f[1]) == p.pid:
                try:
                    cmd = open(f"/proc/{d}/cmdline").read().replace("\0", " ").strip()
                except OSError:
                    cmd = "?"
                out.append(f"{f[0]}:{comm}:{cmd}:pid={d}:pgrp={f[2]}")
    return out


body = [
    b"/bin/echo hi | /bin/cat | /bin/cat > o.txt",
    b"/bin/echo hi >> o.txt",
    b"/bin/sh -c 'echo E >&2' 2>&1 | /bin/cat > /dev/null",
    b"/nonexistent/prog | /bin/cat",
    b"/usr/bin/true | /nonexistent/prog | /bin/cat",
    b"/usr/bin/true | /bin/cat | /nonexistent/prog",
    b"/bin/cat < missing-input.txt",
    b"/bin/echo x > missing-dir/x",
    b"/bin/sleep 0.01 &",
    b"wait",
    b"/bin/sh -c 'sleep 0.05 &' | /bin/cat",
]
sync()
for line in body:  # warm-up round: one-time state (Host loop wake pipe etc.) is not a leak
    p.stdin.write(line + b"\n")
sync()
time.sleep(0.3)
sync()
before = fds()
kids_before = kids()
for _ in range(ROUNDS):
    for line in body:
        p.stdin.write(line + b"\n")
    sync()
time.sleep(0.3)
sync()
after = fds()
kids_after = kids()
for _ in range(3):  # allow the host loop's asynchronous closure to settle
    if len(after) <= len(before) and not any(k.startswith("Z") for k in kids_after):
        break
    time.sleep(0.3)
    after = fds()
    kids_after = kids()
print(f"fds before={len(before)} after={len(after)} delta={len(after) - len(before)} "
      f"children before={kids_before} after={kids_after} zombies_after={sum(k.startswith('Z') for k in kids_after)}")
extra = [fd for fd in after if fd not in before]
for fd in extra[:10]:
    try:
        print(f"  leaked fd {fd} -> {os.readlink(f'/proc/{p.pid}/fd/{fd}')}")
    except OSError:
        pass
p.stdin.write(b"exit\n")
p.stdin.close()
p.wait(timeout=10)
leaked = len(after) > len(before) or any(k.startswith("Z") for k in kids_after)
print("RESULT", "LEAK" if leaked else "CLEAN")
sys.exit(1 if leaked else 0)
