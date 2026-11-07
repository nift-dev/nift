// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): push into an array nothing else holds; the pushed element survives the array's release.
e := [42]
b := [e]
keep := b
g := () => { b = [7]; keep = [8]; return e }
b.push(g())
print(e.stringify())
print(b.stringify())
print(keep.stringify())
