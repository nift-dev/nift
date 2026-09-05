#!/usr/bin/env python3
"""BH5 guard: parser / value / composition adversarial.

Guarantee: adversarial parser inputs and value/composition interactions —
truncated or invalid JSON bound through @json, deep nesting, type mismatches,
missing/null/coerced values, unicode, missing or cyclic @input fragments, and
@pathto edge cases — must resolve with a controlled outcome: a successful
build with correct output, or a controlled non-zero error. A hang, a signal
termination (segfault etc.), a sanitizer finding, or silently wrong output is a
defect this guard flags.
"""
import argparse, json, subprocess, tempfile
from pathlib import Path

SANITIZER_NEEDLES = (
    'AddressSanitizer', 'runtime error:', 'LeakSanitizer',
    'UndefinedBehaviorSanitizer', 'AddressSanitizer:DEADLYSIGNAL',
)


def run(nift, cwd, *args, timeout=20.0):
    try:
        p = subprocess.run([nift, *args], cwd=cwd, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"{' '.join(args)} hung (timeout {timeout}s)")
    combined = p.stdout + p.stderr
    for needle in SANITIZER_NEEDLES:
        if needle in combined:
            raise RuntimeError(f"{' '.join(args)} sanitizer finding {needle}")
    if p.returncode < 0:
        raise RuntimeError(f"{' '.join(args)} terminated by signal {-p.returncode}")
    return p


def setup(root: Path, template: str, site: str, extra: dict[str, str]) -> None:
    subprocess.run([NIFT, 'init'], cwd=root, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)
    tr = json.loads((root / '.nift' / 'tracked.json').read_text())
    tr['tracked'] = [{'name': '/', 'title': 'T', 'template': 'templates/template.html'}]
    (root / '.nift' / 'tracked.json').write_text(json.dumps(tr))
    (root / 'content' / 'index.html').write_text('<p>CONTENT</p>\n')
    (root / 'templates' / 'template.html').write_text(template)
    (root / 'data').mkdir(exist_ok=True)
    (root / 'data' / 'site.json').write_text(site)
    for rel, content in extra.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)


# (id, template, site.json, extra files, expect_success, must_contain)
CASES = [
    ("truncated-json", "@json(site, 'data/site.json')\n@content\n", '{"a:', {}, False, None),
    ("invalid-json", "@json(site, 'data/site.json')\n@content\n", 'not json', {}, False, None),
    ("deep-nesting", "@json(site, 'data/site.json')\n@if(site.ok){ok}\n@content\n",
     '{"ok":' + ('[' * 200) + (']' * 200) + '}', {}, True, None),
    ("deep-nesting-valid", "@json(site, 'data/site.json')\n@if(site.ok){ok}\n@content\n",
     '{"ok":true}', {}, True, ['ok']),
    ("type-mismatch-array", "@json(site, 'data/site.json')\n@for(x:site.items){<i>$[x]</i>}\n@content\n",
     '{"items":42}', {}, False, None),
    ("missing-key", "@json(site, 'data/site.json')\n@if(site.nope){x}\n@content\n", '{}', {}, False, None),
    ("null-value", "@json(site, 'data/site.json')\n$[site.x]\n@content\n", '{"x":null}', {}, True, None),
    ("string-as-number", "@json(site, 'data/site.json')\n@if(site.n >= 2){big}\n@content\n",
     '{"n":"5"}', {}, False, None),
    ("number-as-string", "@json(site, 'data/site.json')\n$[site.s]\n@content\n",
     '{"s":123}', {}, True, ['123']),
    ("unicode", "@json(site, 'data/site.json')\n$[site.s]\n@content\n",
     '{"s":"\\u00e9\\ud83d\\ude00"}', {}, True, None),
    ("unicode-valid", "@json(site, 'data/site.json')\n$[site.s]\n@content\n",
     '{"s":"\\u00e9"}', {}, True, ['\u00e9']),
    ("input-missing", "@input('missing.html')\n@content\n", '{}', {}, False, None),
    ("input-cyclic", "@input('frag.html')\n@content\n", '{}',
     {'templates/frag.html': '@input("template.html")\n'}, False, None),
    ("pathto-missing", '<a href="@pathto(\'nope\')">x</a>\n@content\n', '{}', {}, False, None),
    ("for-over-object", "@json(site, 'data/site.json')\n@for(k:site.obj){<i>$[k]</i>}\n@content\n",
     '{"obj":{"a":1,"b":2}}', {}, False, None),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    args = ap.parse_args()
    global NIFT
    NIFT = str(Path(args.nift).resolve())
    passed = 0
    with tempfile.TemporaryDirectory(prefix='nift-bh5.') as td:
        base = Path(td)
        for case_id, template, site, extra, expect_success, must_contain in CASES:
            root = base / case_id
            root.mkdir()
            setup(root, template, site, extra)
            p = run(NIFT, root, 'build', '--all')
            if expect_success:
                if p.returncode != 0:
                    raise RuntimeError(f"{case_id}: expected success but exited {p.returncode}")
                out_path = root / 'public' / 'index.html'
                if not out_path.is_file():
                    raise RuntimeError(f"{case_id}: expected output but none produced")
                # every success case carries a semantic oracle: the @content
                # block plus any case-specific rendered value must be present
                out = out_path.read_text()
                if '<p>CONTENT</p>' not in out:
                    raise RuntimeError(f"{case_id}: output missing the rendered @content block")
                for needle in (must_contain or []):
                    if needle not in out:
                        raise RuntimeError(f"{case_id}: output missing {needle!r}")
            else:
                # adversarial input must be REJECTED with a controlled non-zero
                # exit - a silent success that builds nothing (or an empty
                # tolerant parse) is a false green
                if p.returncode == 0:
                    out_path = root / 'public' / 'index.html'
                    if not out_path.is_file() or out_path.stat().st_size == 0:
                        raise RuntimeError(
                            f"{case_id}: malformed input accepted with no output "
                            "(silent success)")
                    raise RuntimeError(
                        f"{case_id}: malformed input accepted with output "
                        "(uncontrolled tolerant success)")
                if p.returncode < 0:
                    raise RuntimeError(f"{case_id}: terminated by signal {-p.returncode}")
            passed += 1
    print(f'BH5 parser/value/composition adversarial: PASS ({passed} cases)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
