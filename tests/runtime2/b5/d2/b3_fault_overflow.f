// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family B: the operation faults after the rhs rebinds the target: legacy's fatal overflow at the statement (previously admitted output 6 6 5 changes; approved)
x := 9223372036854775807
g := () => { x = 5; return 1 }
try { x += g() } catch (e) { print("caught") }
print(x)
y := 9223372036854775807
h := () => { y = 5; return 1 }
try { print(y + h()) } catch (e) { print("caught") }
print(y)
