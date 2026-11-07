// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): insert whose index argument rebinds the receiver (legacy [7], [5,1]).
b := [1]
keep := b
gi := () => { b = [7]; return 0 }
b.insert(gi(), 5)
print(b.stringify())
print(keep.stringify())
