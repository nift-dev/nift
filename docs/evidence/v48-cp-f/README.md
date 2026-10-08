# CP-F before matrix (investigation, not a green regression suite)

Run from the Nift repository root:

```sh
mkdir -p .build/cp-f
cp docs/evidence/v48-cp-f/probe.cpp docs/evidence/v48-cp-f/matrix.py .build/cp-f/
c++ -std=c++17 -O2 -Isrc -Iinclude -Iminifypp/include -Imarkuppp/include .build/cp-f/probe.cpp libnift_c.a -ldl -pthread -o .build/cp-f/probe
python3 .build/cp-f/matrix.py
python3 .build/cp-f/matrix.py --check
```

The runner creates its fixtures in `.build/cp-f/cases`. It records both public
error fields and typed diagnostic origin/code/frames directly as JSON. Normal
mode records observations; `--check` asserts canonical paths/lines and stable
columns, plus successful try/catch behavior. On f2f243c the canonical check
intentionally exits 1 with 39 mismatches. This suite must not be labeled green.

`before.json` and `before.log` were captured at f2f243c (runtime ab687d0).
Original source fixtures are included for inspection, including module/include
boundary files. Expected columns marked nonstable are anchors only; the check
does not guess a new subexpression anchoring convention. The public and typed
origin projections are both asserted. Successful try/catch is not expected to
produce an uncaught diagnostic.

The probe links the existing optimized embedding archive. The docs-only changes
in this investigation cannot alter the observed parser behavior. After an
approved implementation, preserve exact before message/code/frame contracts and
qualify any attribution-only changes to frame sites separately.
