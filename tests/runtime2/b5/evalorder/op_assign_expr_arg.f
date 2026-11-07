// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): an assignment expression in the argument: the push goes to the original (legacy [7], [1,[7]]; before the repair: a reference-cycle error).
b := [1]
keep := b
b.push(b = [7])
print(b.stringify())
print(keep.stringify())
