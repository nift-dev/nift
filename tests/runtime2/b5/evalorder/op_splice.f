// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): splice: receiver first, all arguments, then one commit (legacy [7,8], [2]).
b := [1, 2]
keep := b
gi := () => { b = [7, 8]; return 0 }
b.splice(gi(), 1)
print(b.stringify())
print(keep.stringify())
