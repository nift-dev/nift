// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the argument rebinds b and the alias: the retained receiver is the only reference; the removed element is the original's (same identity as e).
e := [42]
b := [e, 2]
keep := b
g := () => { b = [7]; keep = [8]; return 0 }
r := b.remove(g())
print(r.stringify())
print(same(r, e))
print(b.stringify())
print(keep.stringify())
