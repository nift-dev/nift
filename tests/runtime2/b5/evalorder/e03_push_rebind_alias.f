// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the alias keeps the original, which receives the push (legacy [7], [1,4]).
b := [1]
keep := b
g := () => { b = [7]; return 4 }
b.push(g())
print(b.stringify())
print(keep.stringify())
