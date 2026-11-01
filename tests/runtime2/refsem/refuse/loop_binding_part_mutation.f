// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review j06: the
// loop binding is a place in the iterated array (it writes through on the VM
// and in legacy, and would in Phase B), but the guard refuses every in-place
// mutation of an array inside its own loop: it cannot tell this write-through
// from one Phase B would change (conservative).
print("start")
a := [[1], [2]]
for (r : a) { r.push(0); print(r.stringify()) }
print(a.stringify())
