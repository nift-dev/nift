#!/usr/bin/env python3
"""BH3-BH10 repair-tranche attack battery.

Replays the independent reviewer's oracle attacks against the repaired guards.
Each attack must go RED (exit != 0); real Nift must go GREEN (exit 0). This is
the retained red-run evidence for the repair tranche.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
NIFT = str((REPO / "nift").resolve())


def _run(cmd, cwd=None, timeout=300):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(cwd or REPO), timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        return 124, (exc.stdout or "") + (exc.stderr or "")
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def make_wrapper(name, body):
    p = Path(tempfile.mkdtemp(prefix="bh-repair.")) / name
    p.write_text("#!/bin/sh\n" + body)
    p.chmod(0o755)
    return str(p)


def main() -> int:
    guards = [
        ("bh4", "incremental_state_transitions_adversarial.py",
         [("real-nift", NIFT, 0),
          ("do-nothing-incremental", make_wrapper("dn", f"real={NIFT}\n" + 'if [ "$1" = "build" ]; then exit 0; fi\n' + '"$real" "$@"\nexit $?\n'), 1)]),
        ("bh5", "parser_value_composition_adversarial.py",
         [("real-nift", NIFT, 0),
          ("always-success-nothing", make_wrapper("silent", f"real={NIFT}\n" + 'if [ "$1" = "init" ]; then exec "$real" "$@"; fi\nexit 0\n'), 1)]),
        ("bh8", "complexity_invariants.py",
         [("real-nift", NIFT, 0),
          ("stale-rebuild-on-change", make_wrapper("stale", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && grep -q 7-CHANGED content/p7.html 2>/dev/null && [ -f public/p7.html ]; then chmod u+w public/p7.html; echo "<p>0</p>" > public/p7.html; fi\nexit $rc\n'), 1)]),
        ("bh9", "filesystem_boundary_adversarial.py",
         [("real-nift", NIFT, 0),
          ("fake-exit-1-nothing", make_wrapper("fake", "exit 1\n"), 1),
          ("corrupt-source", make_wrapper("csrc", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && [ -f content/index.html ]; then chmod u+w content/index.html; echo corrupted >> content/index.html; fi\nexit $rc\n'), 1)]),
        ("bh3", "pagination_incremental_equivalence.py",
         [("real-nift", NIFT, 0),
          ("corrupt-rendered-content", make_wrapper("cc", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && [ -d public ]; then for f in public/blog*.html; do chmod u+w "$f"; sed -i "s/<section>[^<]*<\\/section>/<section>XX<\\/section>/" "$f"; done; fi\nexit $rc\n'), 1)]),
        ("bh3", "pagination_incremental_equivalence.py",
         [("real-nift", NIFT, 0),
          ("redistributed-per-page-split", make_wrapper("redist", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && [ -d public ]; then\nfor f in public/blog*.html; do chmod u+w "$f"; done\npython3 - <<PY\nfrom pathlib import Path\nimport re\npub=Path("public")\ndef setsec(p, s):\n    t=p.read_text(); t=re.sub(r"<section>.*?</section>", "<section>"+s+"</section>", t, flags=re.S); p.write_text(t)\nsetsec(pub/"blog.html", "a")\nsetsec(pub/"blog-2.html", "bcde")\nsetsec(pub/"blog-3.html", "")\nPY\nfi\nexit $rc\n'), 1)]),
        ("bh5", "parser_value_composition_adversarial.py",
         [("real-nift", NIFT, 0),
          ("wrong-success-output", make_wrapper("wrong", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && [ -f public/index.html ]; then chmod u+w public/index.html; echo WRONG > public/index.html; fi\nexit $rc\n'), 1)]),
        ("bh7", "crash_recovery_adversarial.py",
         [("real-nift", NIFT, 0),
          ("non-atomic-metadata", make_wrapper("corrupt", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ -f .nift/tracked.json ]; then echo \'{"tracked":\' > .nift/tracked.json; fi\nexit $rc\n'), 1),
          ("sleepy-wrapper-neutralized", make_wrapper("sleepy", "sleep 0.2\n" + f"exec {NIFT} \"$@\"\n"), 0)]),
        ("bh8", "complexity_invariants.py",
         [("real-nift", NIFT, 0),
          ("rewrite-unrelated-pages", make_wrapper("tothers", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && grep -q 7-CHANGED content/p7.html 2>/dev/null; then for f in public/*.html; do [ "$f" = public/p7.html ] || { chmod u+w "$f"; touch "$f"; }; done; fi\nexit $rc\n'), 1)]),
        ("bh9", "filesystem_boundary_adversarial.py",
         [("real-nift", NIFT, 0),
          ("index-only-fake", make_wrapper("fake", 'if [ "$1" = "build-all" ] && [ -f .nift/tracked.json ]; then\n  mkdir -p public; echo hi > public/index.html; exit 0\nfi\nexit 1\n'), 1)]),
        ("bh3", "pagination_incremental_equivalence.py",
         [("real-nift", NIFT, 0),
          ("wrong-current-nav", make_wrapper("badnav", f"""real={NIFT}
"$real" "$@"
rc=$?
if [ "$rc" = 0 ] && [ -d public ]; then
  for f in public/blog-*.html; do
    [ -f "$f" ] || continue
    sed -E -i 's#<nav>[0-9]+/([0-9]+) #<nav>1/\\1 #' "$f"
  done
fi
exit $rc
"""), 1)]),
        ("bh4", "incremental_state_transitions_adversarial.py",
         [("real-nift", NIFT, 0),
          ("same-wrong-content-in-incremental-and-clean", make_wrapper("wrongboth", f"""real={NIFT}
"$real" "$@"
rc=$?
if [ "$rc" = 0 ] && {{ [ "$1" = build ] || [ "$1" = build-all ]; }} && grep -q B2 content/b.html 2>/dev/null && ! grep -q 'T2:' templates/template.html 2>/dev/null && [ -f public/b.html ]; then
  chmod u+w public/b.html; echo '<p>WRONG</p>' > public/b.html
fi
exit $rc
"""), 1)]),
        ("bh9", "filesystem_boundary_adversarial.py",
         [("real-nift", NIFT, 0),
          ("wrong-valid-page-content", make_wrapper("wrongpages", f"""real={NIFT}
"$real" "$@"
rc=$?
if [ "$rc" = 0 ] && [ "$1" = build-all ] && [ -d public ]; then
  find public -type f -name '*.html' -exec sh -c 'chmod u+w "$1"; echo WRONG > "$1"' _ {{}} \\;
fi
exit $rc
"""), 1),
          ("valid-build-returns-nonzero", make_wrapper("nonzero", f"""real={NIFT}
"$real" "$@"
rc=$?
if [ "$1" = build-all ] && [ "$rc" = 0 ]; then exit 7; fi
exit $rc
"""), 1)]),
        ("bh6", "init_scaffold_functional_truth.py",
         [("real-nift", NIFT, 0),
          ("minimal-scaffold", make_wrapper("min", f"real={NIFT}\n" + '"$real" "$@"\nrc=$?\nif [ "$rc" = 0 ] && [ "$1" = "init" ] && [ -d content ]; then rm -rf content/assets public/assets; python3 -c "import json; p=\'.nift/tracked.json\'; d=json.load(open(p)); d[\'tracked\']=[e for e in d[\'tracked\'] if not e[\'name\'].startswith(\'assets\')]; open(p,\'w\').write(json.dumps(d))"; fi\nexit $rc\n'), 1)]),
    ]

    results = []
    all_ok = True
    with tempfile.TemporaryDirectory(prefix="bh-repair-battery.") as td:
        for bh, guard, cases in guards:
            gpath = REPO / "tests" / guard
            entry = {"bh": bh, "guard": guard, "cases": []}
            for name, binary, expected in cases:
                print(f"RUN {bh}:{guard}:{name}", flush=True)
                rc, out = _run(["python3", str(gpath), "--nift", binary])
                entry["cases"].append({"name": name, "exit": rc,
                                       "expected_nonzero" if expected else "expected_zero": True,
                                       "red_as_expected": (rc != 0) == bool(expected)})
                if (rc != 0) != bool(expected):
                    all_ok = False
            results.append(entry)

    report = {
        "schema_version": 1,
        "campaign": "bh3-10-repair",
        "checkpoint": "guard-oracle-repair-tranche",
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "platform": "Linux (repair tranche)",
        "nift": NIFT,
        "purpose": "Replays the independent reviewer's oracle attacks against the repaired BH3-BH10 guards: real Nift must stay GREEN and each representative attack must go RED.",
        "all_red_correct": all_ok,
        "guards": results,
    }
    out = Path("/tmp/opencode/bh-repair-battery.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"report={out} all_red_correct={all_ok}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
