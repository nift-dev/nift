// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family B: `/=` on an int binding whose result is a double: legacy's binding-type error at the statement, after the rhs ran (previously admitted output 5 changes; approved)
x := 6
g := () => { x = 20; return 4 }
print("before")
x /= g()
print(x)
