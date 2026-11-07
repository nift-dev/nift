// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): an index read before a method: the container as evaluated (legacy x).
b := [" x "]
fn(g()) { b = [" y "]; return 0 }
r := b[g()].trim()
print(r)
