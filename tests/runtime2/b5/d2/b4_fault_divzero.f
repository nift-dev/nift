// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family B: division by zero after a rebinding rhs, legacy's error and position
x := 6.0
g := () => { x = 5.0; return 0 }
try { x /= g() } catch (e) { print("caught") }
print(x)
