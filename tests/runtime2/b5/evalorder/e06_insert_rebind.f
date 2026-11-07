// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): insert: receiver first (legacy [7], [4,1]).
b := [1]
keep := b
g := () => { b = [7]; return 4 }
b.insert(0, g())
print(b.stringify())
print(keep.stringify())
