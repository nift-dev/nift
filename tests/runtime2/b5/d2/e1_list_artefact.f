// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29): `[x, g()]` (out of scope) was a probe artefact; list items and call arguments already run left to right (pinned)
x := 1
g := () => { x = 10; return 1 }
print([x, g()].stringify())
x = 1
fn(f(a, c)) { return a * 100 + c }
print(f(x, g()))
print([x, g()].stringify())
