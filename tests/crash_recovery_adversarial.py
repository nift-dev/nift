#!/usr/bin/env python3
"""BH7 guard: persistence / crash / recovery adversarial.

Guarantee: interrupting Nift mid-build (SIGKILL at multiple points during
build-all) leaves the project crash-safe: the tracked and config metadata
remain valid JSON, the next build succeeds, and a further build converges to
the clean output. A tool whose metadata writes are non-atomic (corrupt after a
crash) or whose output never converges is caught here.
"""
import argparse, hashlib, json, shutil, subprocess, tempfile, time
from pathlib import Path


def tree_hash(root: Path) -> dict[str, str]:
    if not root.exists():
        return {}
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(x for x in root.rglob('*') if x.is_file())}


def setup(root: Path, mode: str, n: int = 40) -> None:
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    ap.add_argument('--kills', default='0.0,0.02,0.08')
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    kill_points = [float(x) for x in args.kills.split(',')]

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

            for kp in kill_points:
                proc = subprocess.Popen([nift, 'build-all'], cwd=root,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(kp)
                proc.kill()
                proc.wait()
                assert_valid_json(root, f'{mode}:kill-{kp}')
                if not build_ok(nift, root):
                    raise RuntimeError(f"{mode}:kill-{kp}: next build failed after crash")

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
