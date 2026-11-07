// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): an index read whose key drops every other reference to the container: the element is the original's.
e := [42]
b := [e, 2]
keep := b
g := () => { b = [7]; keep = [8]; return 0 }
r := b[g()]
print(same(r, e))
print(b.stringify())
print(keep.stringify())
