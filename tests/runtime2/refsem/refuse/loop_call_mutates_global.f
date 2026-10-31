// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): the loop body calls a function that mutates
// the iterated global.
print("start")
a := [1, 2]
fn(grow()) { a.push(7); return 0 }
for (x : a) { grow(); print(x) }
print(a.length())
