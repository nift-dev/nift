// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): element store: receiver first, then the value (legacy [7,8], [4,2]).
b := [1, 2]
keep := b
g := () => { b = [7, 8]; return 4 }
b[0] = g()
print(b.stringify())
print(keep.stringify())
