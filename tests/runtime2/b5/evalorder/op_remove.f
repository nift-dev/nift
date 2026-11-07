// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): remove: receiver first (legacy [7,8], [2]).
b := [1, 2]
keep := b
gi := () => { b = [7, 8]; return 0 }
b.remove(gi())
print(b.stringify())
print(keep.stringify())
