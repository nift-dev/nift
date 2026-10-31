// D-NEXT-refsem (D2 guard): passing a mutated array to a function that only reads it.
fn(sum(xs)) { t := 0; for (x : xs) { t += x }; return t }
a := [1, 2]
print(sum(a))
a.push(3)
print(sum(a))
