// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): an element store used as an expression (legacy 4, [7,8], [4,2]).
b := [1, 2]
keep := b
g := () => { b = [7, 8]; return 4 }
print(b[0] = g())
print(b.stringify())
print(keep.stringify())
