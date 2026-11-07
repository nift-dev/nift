// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family A: binary operators read the left operand before the right; legacy-equal (legacy 10 2 24 1.5 2 true false true false aa T)
// expected: 10 2 24 1.5 2 true false true false aa T
x := 6
fn(g()) { x = 20; return 4 }
print(x + g())
x = 6
print(x - g())
x = 6
print(x * g())
x = 6
print(x / g())
x = 6
print(x % g())
x = 6
print(x == g() + 2)
x = 6
print(x != g() + 2)
x = 6
print(x < g() + 4)
x = 6
print(x > g() + 4)
s := "a"
fn(k()) { s = "Z"; return "a" }
print(s + k())
x = 6
if (x == g() + 2) { print("T") } else { print("F") }
