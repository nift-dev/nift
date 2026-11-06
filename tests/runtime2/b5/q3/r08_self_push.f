// Q3 row 8 (§3 Cycles, D1): refused before the mutation, catchable.
a := [1, 2]
try { a.push(a) } catch (e) { print(e.code) }
print(a.stringify())
