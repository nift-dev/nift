#!/usr/bin/env python3
"""CP03 B1/B2: explicit NIFT_FS_ROOT stays effective for cached requirements
and for project build hooks; unrestricted builds keep the fast path."""
import json, os, subprocess, sys, tempfile
from pathlib import Path

NIFT = os.path.abspath(sys.argv[1])
ROOT = Path(tempfile.mkdtemp(prefix="cp03-restricted-"))


def site(name, body="", hook=None, global_hook=None):
    p = ROOT / name
    for d in (".nift", "content", "templates", "public", "scripts", "data"):
        (p / d).mkdir(parents=True, exist_ok=True)
    cfg = {"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
                      "output-ext": ".html", "default-template": "templates/t.html", "build-threads": 1,
                      "incremental-mode": "modified", "minify-exts": []}}
    if global_hook:
        cfg["config"]["pre build"] = "scripts/global.f"
    tracked = {"name": "index", "title": "Index"}
    if hook:
        tracked["pre-build"] = "scripts/pre.f"
    (p / ".nift/config.json").write_text(json.dumps(cfg))
    (p / ".nift/tracked.json").write_text(json.dumps({"tracked": [tracked]}))
    (p / "templates/t.html").write_text("@content")
    (p / "content/index.html").write_text(body)
    (p / "data/inside.txt").write_text("INSIDE")
    if hook:
        (p / "scripts/pre.f").write_text(hook)
    if global_hook:
        (p / "scripts/global.f").write_text(global_hook)
    return p


def run(p, label, *args, restricted=True):
    env = dict(os.environ)
    if restricted:
        env["NIFT_FS_ROOT"] = str(p)
    r = subprocess.run([NIFT, *args], cwd=p, env=env, text=True, capture_output=True, timeout=60)
    out = (r.stdout or "") + (r.stderr or "")
    print(f"{label}: exit={r.returncode}")
    return r.returncode, out


def expect(ok, label, detail=""):
    if not ok:
        print(f"FAIL {label}: {detail}")
        raise SystemExit(1)
    print(f"PASS {label}")


def retarget(kind, form):
    p = site(f"retarget-{kind}-{form}")
    outside = ROOT / f"outside-{kind}-{form}.txt"
    outside.write_text("OUT")
    if kind == "leaf":
        inside = p / "public/inside.txt"; inside.write_text("IN")
        body = f'@{form}("public/link.txt")'
        target = p / "public/link.txt"
        link = target
        initial = inside
        final = outside
    else:
        inside_dir = p / "public/inside-dir"; inside_dir.mkdir(exist_ok=True)
        (inside_dir / "file.txt").write_text("IN")
        outside_dir = ROOT / f"outside-dir-{form}"; outside_dir.mkdir(exist_ok=True)
        (outside_dir / "file.txt").write_text("OUT")
        body = f'@{form}("public/alias/file.txt")'
        link = p / "public/alias"
        initial = inside_dir
        final = outside_dir
    (p / "content/index.html").write_text(body)
    link.symlink_to(initial)
    rc, out = run(p, f"restricted {form} {kind} initial", "build", "--all")
    expect(rc == 0, f"restricted {form} {kind} initial build", out[-300:])
    link.unlink(); link.symlink_to(final)
    rc, out = run(p, f"restricted {form} {kind} retarget no-op", "build")
    expect(rc != 0 and "path escapes configured filesystem root" in out,
           f"restricted {form} {kind} retarget rejected", out[-400:])
    rc, out = run(p, f"restricted {form} {kind} retarget forced", "build", "--all")
    expect(rc != 0 and "path escapes configured filesystem root" in out,
           f"restricted {form} {kind} forced retarget rejected", out[-400:])
    # Unrestricted control on a separate project.
    q = site(f"free-{kind}-{form}")
    (q / "content/index.html").write_text(body)
    if kind == "leaf":
        (q / "public/inside.txt").write_text("IN")
        qlink = q / "public/link.txt"
        qinitial = q / "public/inside.txt"
    else:
        qdir = q / "public/inside-dir"; qdir.mkdir(exist_ok=True); (qdir / "file.txt").write_text("IN")
        qlink = q / "public/alias"
        qinitial = qdir
    qlink.symlink_to(qinitial)
    rc, out = run(q, f"free {form} {kind} initial", "build", "--all", restricted=False)
    expect(rc == 0, f"free {form} {kind} initial build", out[-300:])
    qlink.unlink(); qlink.symlink_to(final)
    rc, out = run(q, f"free {form} {kind} retarget", "build", restricted=False)
    expect(rc == 0 and "path escapes configured filesystem root" not in out,
           f"free {form} {kind} retarget stays fast", out[-400:])

def transition():
    p = site("transition")
    inside = p / "public/inside.txt"; inside.write_text("IN")
    outside = ROOT / "transition-outside.txt"; outside.write_text("OUT")
    (p / "content/index.html").write_text('@path("public/link.txt")')
    (p / "public/link.txt").symlink_to(inside)
    rc, out = run(p, "transition free initial", "build", "--all", restricted=False)
    expect(rc == 0, "transition free initial build", out[-300:])
    (p / "public/link.txt").unlink(); (p / "public/link.txt").symlink_to(outside)
    rc, out = run(p, "transition enable restriction", "build")
    expect(rc != 0 and "path escapes configured filesystem root" in out,
           "enabling NIFT_FS_ROOT revalidates an outside requirement", out[-400:])


def hooks():
    outside = ROOT / "hook-secret.txt"
    outside.write_text("SECRET-OUTSIDE")
    bad = f'print(cat("{outside}"))\n'
    good = 'print(cat("data/inside.txt"))\n'
    for label, hook, global_hook in (("per-item", bad, None), ("global", None, bad)):
        p = site(f"hook-bad-{label}", "OK", hook, global_hook)
        rc, out = run(p, f"restricted {label} hook", "build", "--all")
        expect(rc != 0 and "path escapes configured filesystem root" in out,
               f"restricted {label} hook refused", out[-400:])
    for label, hook, global_hook in (("per-item", good, None), ("global", None, good)):
        p = site(f"hook-good-{label}", "OK", hook, global_hook)
        rc, out = run(p, f"restricted {label} inside hook", "build", "--all")
        expect(rc == 0 and "INSIDE" in out, f"restricted {label} inside hook allowed", out[-300:])


def main():
    for kind in ("leaf", "parent"):
        for form in ("path", "pathto"):
            retarget(kind, form)
    transition()
    hooks()
    print("PASS CP03 restricted policy probes")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    finally:
        import shutil
        shutil.rmtree(ROOT, ignore_errors=True)
