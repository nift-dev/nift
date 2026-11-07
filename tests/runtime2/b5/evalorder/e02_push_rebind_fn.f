// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): the same with a `fn` callee writing the global (legacy [7]).
b := [1]
fn(g()) { b = [7]; return 4 }
b.push(g())
print(b.stringify())
