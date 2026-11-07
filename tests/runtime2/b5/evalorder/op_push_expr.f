// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): push as an expression (legacy 4, [7], [1,4]).
b := [1]
keep := b
g := () => { b = [7]; return 4 }
n := b.push(g())
print(n)
print(b.stringify())
print(keep.stringify())
