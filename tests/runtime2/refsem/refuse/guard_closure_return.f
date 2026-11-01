// D-NEXT-refsem #1 (D2 guard): a closure returning a captured array that is mutated.
print("start")
a := [1]
g := () => a
x := g()
a.push(2)
print(x.stringify())
