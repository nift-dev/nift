#!/usr/bin/env python3
"""BH7 guard: persistence / crash / recovery adversarial.

Guarantee: interrupting Nift mid-build (SIGKILL at multiple points during
build-all) leaves the project crash-safe: the tracked and config metadata
remain valid JSON, the next build succeeds, and a further build converges to
the clean output. A tool whose metadata writes are non-atomic (corrupt after a
crash) or whose output never converges is caught here.
"""
import argparse, hashlib, json, os, shutil, signal, subprocess, tempfile, time
from pathlib import Path


def tree_hash(root: Path) -> dict[str, str]:
    if not root.exists():
        return {}
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(x for x in root.rglob('*') if x.is_file())}


def setup(root: Path, mode: str, n: int = 6000) -> None:
    (root / '.nift').mkdir()
    (root / 'content').mkdir()
    (root / 'templates').mkdir()
    (root / 'public').mkdir()
    (root / '.nift' / 'config.json').write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/template.html",
        "build-threads": -1, "incremental-mode": mode}}))
    (root / '.nift' / 'tracked.json').write_text(json.dumps({"tracked": [
        {"name": f"p{i}", "title": f"P{i}", "template": "templates/template.html"}
        for i in range(n)]}))
    (root / 'templates' / 'template.html').write_text('@content\n')
    for i in range(n):
        (root / 'content' / f'p{i}.html').write_text(f'<p>{i}</p>\n')


def build_ok(nift: str, root: Path) -> bool:
    try:
        subprocess.run([nift, 'build-all'], cwd=root, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        return True
    except Exception:
        return False


def kill_during_build(nift: str, root: Path, label: str, timeout: float = 10.0) -> None:
    """Start a build-all, wait for OBSERVABLE build-write progress (the first
    output file appearing in public/), then SIGKILL the still-live process so
    the crash genuinely interrupts transactional/build activity. Fails (rather
    than passing) if the process exits before any output is written, or if
    SIGKILL does not terminate a live process."""
    public = root / 'public'
    shutil.rmtree(public, ignore_errors=True)
    public.mkdir(exist_ok=True)
    shutil.rmtree(root / '.nift' / 'public', ignore_errors=True)
    # Put the command in its own process group. This matters for test-of-test
    # wrappers: killing only a shell wrapper can leave the real Nift child
    # running, which would make a red-team crash attack meaningless.
    proc = subprocess.Popen([nift, 'build-all'], cwd=root, start_new_session=True,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + timeout
    progress = False
    while time.time() < deadline:
        if proc.poll() is not None:
            proc.wait()
            raise RuntimeError(
                f"{label}: build finished before any output was written "
                f"(rc={proc.returncode}); NOT a crash test")
        if public.exists() and any(public.iterdir()):
            progress = True
            break
        time.sleep(0.002)
    if not progress:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        raise RuntimeError(f"{label}: no observable build progress within {timeout}s")
    if proc.poll() is not None:
        proc.wait()
        raise RuntimeError(f"{label}: build finished between progress and SIGKILL")
    os.killpg(proc.pid, signal.SIGKILL)
    proc.wait()
    if proc.returncode != -signal.SIGKILL:
        raise RuntimeError(
            f"{label}: SIGKILL did not terminate a live mid-build process "
            f"(rc={proc.returncode})")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    ap.add_argument('--kills', type=int, default=2)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    kill_count = args.kills

    def assert_valid_json(root: Path, label: str) -> None:
        for rel in ('.nift/tracked.json', '.nift/config.json'):
            p = root / rel
            try:
                json.loads(p.read_text())
            except Exception as exc:
                raise RuntimeError(f"{label}: {rel} is not valid JSON after crash ({exc})")

    with tempfile.TemporaryDirectory(prefix='nift-bh7.') as td:
        base = Path(td)
        for mode in ('modified', 'hash', 'hybrid'):
            root = base / mode
            root.mkdir()
            setup(root, mode)
            if not build_ok(nift, root):
                raise RuntimeError(f"{mode}: baseline build failed")
            assert_valid_json(root, f'{mode}:baseline')
            baseline = tree_hash(root / 'public')

            for i in range(kill_count):
                label = f'{mode}:crash-{i}'
                kill_during_build(nift, root, label)
                assert_valid_json(root, label)
                if not build_ok(nift, root):
                    raise RuntimeError(f"{label}: next build failed after crash")

            # a further build must converge to the baseline clean output
            if not build_ok(nift, root):
                raise RuntimeError(f"{mode}: final build failed")
            if tree_hash(root / 'public') != baseline:
                raise RuntimeError(f"{mode}: output does not converge after crash recovery")

            fresh = base / f'{mode}-fresh'
            shutil.copytree(root, fresh)
            shutil.rmtree(fresh / 'public')
            (fresh / 'public').mkdir()
            if not build_ok(nift, fresh):
                raise RuntimeError(f"{mode}: clean rebuild failed")
            if tree_hash(fresh / 'public') != baseline:
                raise RuntimeError(f"{mode}: recovered output diverges from clean rebuild")

            print(f'{mode}: PASS')

    print('BH7 persistence/crash/recovery adversarial: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
